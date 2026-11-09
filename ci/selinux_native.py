#!/usr/bin/env python3
"""Explicit Rocky CI-only Enforcing acceptance in fresh state/transient units.

Requires the candidate policy already loaded. No production service is used.
Do not invoke from a PR: the orchestrator must pin reviewed main first.
"""
import argparse
import asyncio
import json
import os
from pathlib import Path
import re
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests'), str(ROOT / 'scripts')]


def run(*args, check=True):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=120)
    if check and result.returncode:
        raise RuntimeError('fixture command failed: ' + str(args[0]) + '\n' + result.stderr[-4096:])
    return result


def fixture_denials(text):
    """Export only AVC records for the dedicated fixture, never other audit data."""
    domain = re.compile(r'\bscontext=[^\s:]+:[^\s:]+:liner_notes(?:_(?:web|link|admin|door|transfer))?_t:')
    for line in text.splitlines():
        if re.search(r'\btype=(?:AVC|1400)\s', line) and re.search(r'\bavc:\s+denied\s', line) and domain.search(line):
            yield line[:4096]


class Fixture:
    def __init__(self):
        import pwd
        if os.geteuid() != 0 or run('getenforce').stdout.strip() != 'Enforcing':
            raise ValueError('This acceptance requires root and Enforcing, never changes either')
        if '10.0.0.51' not in run('hostname', '-I').stdout.split():
            raise ValueError('Only the designated Rocky test VM is accepted')
        if 'ID="rocky"' not in Path('/etc/os-release').read_text():
            raise ValueError('Expected the Rocky test VM')
        self.account = pwd.getpwnam('liner_notes_ci')
        caller = pwd.getpwnam('ci-test')
        if (self.account.pw_uid in (0, caller.pw_uid) or self.account.pw_gid == 0 or
                Path(self.account.pw_shell).name != 'nologin'):
            raise ValueError('A separate non-login fixture account is required')
        self.path = Path(tempfile.mkdtemp(prefix='liner-notes-selinux-ci-', dir='/var/lib'))
        self.path.chmod(0o755)
        run('chcon', '-t', 'liner_notes_ro_t', self.path)
        self.units, self.ports, self.results = [], [], []
        self.record = ROOT.parent / 'fixture-state.json'
        if self.record.exists():
            raise ValueError('Refuse to overwrite an existing fixture recovery record')
        self.save_record()

    def save_record(self):
        # ROOT.parent is the helper's root-only work directory, never ci-test data.
        temporary = self.record.with_suffix('.new')
        with temporary.open('x', encoding='utf8') as stream:
            json.dump({'fixture': str(self.path), 'units': self.units, 'ports': self.ports}, stream)
            stream.flush()
            os.fsync(stream.fileno())
        temporary.chmod(0o600)
        os.replace(temporary, self.record)

    def label(self, path, kind, recursive=False):
        if not Path(path).resolve().is_relative_to(self.path):
            raise ValueError('Only newly created fixture labels can change')
        run('chcon', *(['-R'] if recursive else []), '-t', kind, path)

    def directory(self, name, kind):
        path = self.path / name
        path.mkdir(mode=0o700)
        os.chown(path, self.account.pw_uid, self.account.pw_gid)
        self.label(path, kind)
        return path

    def text(self, name, text, kind, executable=False):
        path = self.path / name
        path.write_text(text)
        path.chmod(0o755 if executable else 0o644)
        self.label(path, kind)
        return path

    def port(self, kind):
        with socket.socket() as stream:
            stream.bind(('127.0.0.1', 0))
            port = stream.getsockname()[1]
        # -a only: a preexisting mapping must never be overwritten by a test.
        run('semanage', 'port', '-a', '-t', kind, '-p', 'tcp', port)
        self.ports.append(port)
        self.save_record()
        return port

    def start(self, name, executable, arguments, directory, domain):
        unit = self.path.name + '-' + name
        if run('systemctl', 'show', unit, '-p', 'LoadState', '--value').stdout.strip() != 'not-found':
            raise ValueError('Refuse existing service identity')
        self.units.append(unit)
        self.save_record()
        run('systemd-run', '--quiet', '--unit=' + unit,
            '--property=User=' + self.account.pw_name, '--property=Group=' + str(self.account.pw_gid),
            '--property=NoNewPrivileges=yes', '--property=PrivateTmp=yes',
            '--property=ProtectHome=yes', '--property=ProtectSystem=strict',
            '--property=ReadWritePaths=' + str(self.path), '--property=RuntimeMaxSec=300',
            '--property=WorkingDirectory=' + str(directory), '--', executable, *arguments)
        for _ in range(50):
            value = run('systemctl', 'show', unit, '-p', 'MainPID', '--value').stdout.strip()
            if value.isdigit() and int(value):
                try:
                    actual = Path('/proc/' + value + '/attr/current').read_text().strip('\x00\n')
                    if actual.split(':')[2] == domain:
                        self.results.append({'service': name, 'domain': domain, 'pid': int(value)})
                        return unit
                except FileNotFoundError:
                    pass
            time.sleep(.1)
        raise RuntimeError('Expected confined domain was not entered: ' + name)

    def stop(self, unit):
        run('systemctl', 'stop', unit)

    def ready(self, port):
        for _ in range(60):
            try:
                with socket.create_connection(('127.0.0.1', port), timeout=1):
                    return
            except OSError:
                time.sleep(.1)
        raise RuntimeError('Fixture listener did not become ready')

    def close(self):
        for unit in reversed(self.units):
            # All names are fresh fixture-generated units, never installed ones.
            run('systemctl', 'stop', unit, check=False)
            log = run('journalctl', '--no-pager', '-u', unit, '-n', '150', check=False)
            print(log.stdout)
            run('systemctl', 'reset-failed', unit, check=False)
        # The root orchestrator already refuses any pre-existing Liner Notes
        # policy. These domains therefore belong to this isolated acceptance.
        # Do not copy PATH/SYSCALL/EXECVE records or unrelated system denials.
        denials = set()
        if shutil.which('ausearch'):
            try:
                audit = subprocess.run(['ausearch', '-m', 'AVC', '-ts', 'recent', '--raw'],
                                       capture_output=True, text=True, timeout=10, check=False)
                denials.update(fixture_denials(audit.stdout))
            except (OSError, subprocess.TimeoutExpired):
                print('SELINUX_DIAGNOSTIC unavailable; original acceptance result retained')
        # Minimal installations may send AVCs only to the kernel journal.
        # Apply the same narrow filter before exporting any kernel log text.
        try:
            kernel = run('journalctl', '--no-pager', '-k', '--since=-10min', '-o', 'cat', check=False)
            denials.update(fixture_denials(kernel.stdout))
        except (OSError, subprocess.TimeoutExpired):
            print('SELINUX_DIAGNOSTIC kernel journal unavailable')
        for denial in sorted(denials):
            print('SELINUX_DENIAL ' + denial)
        for port in reversed(self.ports):
            run('semanage', 'port', '-d', '-p', 'tcp', port)
        # Keep the private fixture for diagnosis; never erase original BBS data.
        print(json.dumps({'fixture': str(self.path), 'checks': self.results}))


async def bbs_probe(port, create):
    from webui.bridge import Bridge, response_data
    bridges = []
    try:
        for name in ('alice', 'bob'):
            b = await Bridge('127.0.0.1', port, timeout=10).open()
            bridges.append(b)
            await b.command(('REGISTER ' if create else 'LOGIN ') + name)
            await b.command('selinux-only-fixture-password')
            assert b.state['user'], b.state
        a, b = bridges
        if create:
            await a.command('POST confined article')
            async with a.lock:
                await a.literal('Persisted under Enforcing')
            assert 'SAVED' in (await a.command('.'))['text']
            await a.command('MAIL SEND bob confined mail')
            async with a.lock:
                await a.literal('Private mail under Enforcing')
            assert 'MAIL SENT' in (await a.command('.'))['text']
            # READ is contextual while the mail menu remains active.
            await a.command('MAIL EXIT')
        article = response_data((await a.command('READ 1'))['text'])['article']
        assert 'Persisted under Enforcing' in article['body']
        assert 'Private mail under Enforcing' in (await b.command('MAIL READ 1'))['text']
        if create:
            link = await Bridge('127.0.0.1', port, timeout=10).open()
            bridges.append(link)
            await link.command('REGISTER link')
            await link.command('selinux-only-fixture-password')
            assert link.state['user'] == 3, link.state
    finally:
        for b in bridges:
            await b.close()


def door_probe(port):
    from test_telnet import Client
    from test_door_resize import gone
    c = Client(port)
    try:
        c.until()
        time.sleep(1.1)  # Respect the existing authentication cooldown.
        c.cmd('LOGIN alice', b'> ')
        c.cmd('selinux-only-fixture-password')
        c.cmd('GAME', b'0:main] > ')
        for key in (b'q', b'x', None):
            text = c.cmd('1', b'KEY> ')
            match = re.search(rb'CONFINED (\d+) DENIED', text)
            assert match, text
            pid = int(match[1])
            assert Path(f'/proc/{pid}/attr/current').read_text().split(':')[2] == 'liner_notes_door_t'
            if key is None:
                c.close()
            else:
                c.send(key)
                assert b'Back to BBS' in c.until(b'0:main] > ')
            gone(pid)
    finally:
        c.close()


async def web_probe(port):
    import aiohttp
    origin = f'http://127.0.0.1:{port}'
    async with aiohttp.ClientSession(cookie_jar=aiohttp.CookieJar(unsafe=True), headers={'Origin': origin}) as client:
        for _ in range(50):
            try:
                async with client.get(origin + '/notes/') as response:
                    assert response.status == 200
                break
            except aiohttp.ClientConnectorError:
                await asyncio.sleep(.1)
        async with client.post(origin + '/notes/api/session', json={}) as response:
            assert response.status == 200
            client.headers['X-CSRF-Token'] = (await response.json())['csrf']
        await asyncio.sleep(1.1)
        async with client.post(origin + '/notes/api/auth', json={'login':'alice','password':'selinux-only-fixture-password'}) as response:
            data = await response.json()
            assert response.status == 200 and data['state']['user'], data
            client.headers['X-CSRF-Token'] = data['csrf']
        async with client.post(origin + '/notes/api/query', json={'kind':'read','id':1}) as response:
            data = await response.json()
            assert response.status == 200 and 'Persisted under Enforcing' in data['data']['article']['body'], data


def copy_public_code(source, destination):
    """Copy fresh code without inheriting the root source tree's private DAC."""
    shutil.copytree(source, destination, symlinks=True,
                    ignore=shutil.ignore_patterns('__pycache__'))
    for path in (destination, *destination.rglob('*')):
        mode = path.lstat().st_mode
        if stat.S_ISDIR(mode):
            path.chmod(0o755)
        elif stat.S_ISREG(mode):
            path.chmod(0o755 if mode & 0o111 else 0o644)
        else:
            raise ValueError('Unexpected object in fresh public fixture code')


def stage_web_source(source):
    """Publish only fresh fixture code despite the root helper's private umask."""
    source.mkdir(mode=0o755)
    source.chmod(0o755)
    copy_public_code(ROOT / 'webui', source / 'webui')


def web_fixture(fixture, core_port, secret):
    source = fixture.path / 'source'
    stage_web_source(source)
    fixture.label(source, 'liner_notes_ro_t', True)
    data = fixture.directory('web', 'liner_notes_web_data_t')
    port = fixture.port('liner_notes_web_port_t')
    config = fixture.text('web.toml', f'''[web]
listen = "127.0.0.1"
port = {port}
origin = "http://127.0.0.1:{port}"
secure_cookies = false
spool = "{data}"
[bbs]
host = "127.0.0.1"
port = {core_port}
telnet = true
''', 'liner_notes_web_etc_t')
    entry = fixture.text('web-entry', '#!/usr/bin/python3 -B\n'
        'import sys,runpy\nfrom pathlib import Path\n'
        'try:\n'
        f'    Path({str(secret)!r}).read_text()\n'
        'except PermissionError:\n'
        f'    Path({str(data / "MAC_OK")!r}).write_text("denied")\n'
        'else:\n'
        '    raise RuntimeError("Web read BBS original data")\n'
        f'sys.path.insert(0,{str(source)!r})\n'
        'runpy.run_module("webui",run_name="__main__")\n', 'liner_notes_web_exec_t', True)
    unit = fixture.start('web', entry, ['--config', str(config)], data, 'liner_notes_web_t')
    asyncio.run(web_probe(port))
    assert (data / 'MAC_OK').read_text() == 'denied'
    fixture.stop(unit)
    fixture.results.append({'web': 'HTTP login/article and original-data MAC denial'})


async def link_probe(fixture, core_port):
    from cryptography.hazmat.primitives.serialization import load_pem_private_key
    from webui.bridge import Bridge
    import link_transport
    from link_store import Store
    own = fixture.directory('link', 'liner_notes_link_data_t')
    private = own / 'identity'
    own_id = link_transport.generate(private, 'Confined fixture')
    fixture.label(private, 'liner_notes_link_secret_t', True)
    for path in (private, *private.iterdir()):
        os.chown(path, fixture.account.pw_uid, fixture.account.pw_gid)
    peer = fixture.directory('peer', 'liner_notes_ro_t')
    peer_id = link_transport.generate(peer / 'identity', 'Synthetic peer')
    certificate = fixture.text('peer.crt', (peer/'identity/identity.crt').read_text(), 'liner_notes_ro_t')
    password = own / 'password'
    password.write_text('selinux-only-fixture-password\n')
    password.chmod(0o600)
    os.chown(password, fixture.account.pw_uid, fixture.account.pw_gid)
    fixture.label(password, 'liner_notes_link_secret_t')
    port, peer_port = fixture.port('liner_notes_link_port_t'), fixture.port('liner_notes_link_port_t')
    board = 'a'*64
    _, own_public = link_transport.certificate_node(private/'identity.crt')
    _, peer_public = link_transport.certificate_node(certificate)
    origins = {own_id: {'public':own_public,'boards':{board}}, peer_id:{'public':peer_public,'boards':{board}}}
    key = load_pem_private_key((peer/'identity/identity.key').read_bytes(), None)
    store = Store(peer/'peer.sqlite', key, origins, {own_id:origins[own_id]})
    store.accept(store.sign(dict(version=1, origin=peer_id, board=board, article='b'*64,
                                parent=None, revision=1, kind='post', author='remote',
                                title='Confined link import', body='Signed incoming body', created=1)))
    context = link_transport.context(peer/'identity', [private/'identity.crt'], server=True)
    peer_client = link_transport.context(peer/'identity', [private/'identity.crt'])
    server = await asyncio.start_server(lambda r,w: link_transport.serve(store,r,w),
                                       '127.0.0.1', peer_port, ssl=context)
    bridge = await Bridge('127.0.0.1', core_port, timeout=10).open()
    unit = None
    try:
        await bridge.command('LOGIN link')
        await bridge.command('selinux-only-fixture-password')
        assert bridge.state['user'] == 3
        config = fixture.text('link.toml', f'''[node]
identity = "{private}"
database = "{own/'link.sqlite'}"
listen = "127.0.0.1"
port = {port}
interval = 1
rotate_tls = true
[bbs]
host = "127.0.0.1"
port = {core_port}
login = "link"
password_file = "{password}"
[[boards]]
id = "{board}"
local = 1
[[origins]]
certificate = "{certificate}"
boards = ["{board}"]
[[peers]]
certificate = "{certificate}"
boards = ["{board}"]
host = "127.0.0.1"
port = {peer_port}
''', 'liner_notes_link_etc_t')
        scripts = fixture.path / 'source/scripts'
        copy_public_code(ROOT/'scripts', scripts)
        fixture.label(scripts, 'liner_notes_ro_t', True)
        entry = fixture.text('link-entry', '#!/usr/bin/python3 -B\nimport sys,runpy\n'
            f'sys.path.insert(0,{str(scripts)!r})\n'
            f'runpy.run_path({str(scripts/"link_service.py")!r},run_name="__main__")\n',
            'liner_notes_link_exec_t', True)
        unit = fixture.start('link', entry, ['serve','--config',str(config)], own, 'liner_notes_link_t')
        for _ in range(150):
            # Exchanges are pull-based in both directions. The peer server
            # supplies incoming events; it cannot receive the daemon's exports
            # until this peer explicitly pulls from the confined listener.
            try:
                await link_transport.pull(store, own_id, '127.0.0.1', port, peer_client)
            except ConnectionRefusedError:
                await asyncio.sleep(.2)
                continue
            result = await bridge.command('READ 2')
            exports = store.db.execute('SELECT envelope FROM events WHERE origin=?', (own_id,)).fetchall()
            if ('Signed incoming body' in result['text'] and
                    any('Persisted under Enforcing' in json.loads(row[0])['event'].get('body', '') for row in exports)):
                break
            await asyncio.sleep(.2)
        else:
            raise AssertionError('Confined signed import/export did not converge')
        assert (private/'tls-current.json').is_file()
        fixture.results.append({'link':'signed TLS import/export, dedicated domain, TLS rotation'})
    finally:
        if unit:
            # Keep servicing the peer while the daemon finishes an in-flight
            # pull during orderly shutdown; blocking this loop can deadlock it.
            await asyncio.to_thread(fixture.stop, unit)
        await bridge.close()
        server.close()
        await server.wait_closed()
        store.close()


def transfer_fixture(fixture, core, secret):
    receiver = shutil.which('lrz') or shutil.which('rz')
    sender = shutil.which('lsz') or shutil.which('sz')
    if not receiver or not sender:
        raise ValueError('Real lrzsz is required; no mocked transfer acceptance')
    copies = []
    for source, name in ((receiver,'lrz'),(sender,'lsz')):
        target = fixture.path / name
        shutil.copyfile(source, target)
        target.chmod(0o755)
        fixture.label(target, 'bin_t')
        copies.append(target)
    spool = fixture.directory('spool', 'liner_notes_spool_t')
    marker = spool / 'mac-probe'
    scripts = fixture.path / 'transfer-scripts'
    copy_public_code(ROOT/'scripts', scripts)
    fixture.label(scripts, 'liner_notes_ro_t', True)
    arguments = ['--receiver',str(copies[0]),'--sender',str(copies[1]),'--spool-root',str(spool),'--timeout','15','--']
    entry = fixture.text('transfer-entry', '#!/usr/bin/python3 -B\n'
        'import os,sys,runpy\nfrom pathlib import Path\n'
        'try:\n'
        f'    Path({str(secret)!r}).read_text()\n'
        'except PermissionError:\n'
        f'    Path({str(marker)!r}).write_text(str(os.getpid()))\n'
        'else:\n'
        '    raise RuntimeError("Transfer helper read BBS original data")\n'
        f'sys.path.insert(0,{str(scripts)!r})\n'
        f'sys.argv=[{str(scripts/"zmodem_host.py")!r}]+{arguments!r}+sys.argv[1:]\n'
        f'runpy.run_path({str(scripts/"zmodem_host.py")!r},run_name="__main__")\n',
        'liner_notes_transfer_exec_t', True)
    vault = core/'vault'
    run(ROOT/'vaultctl', 'init', vault)
    for path in (vault, *vault.rglob('*')):
        os.chown(path, fixture.account.pw_uid, fixture.account.pw_gid)
    fixture.label(vault, 'liner_notes_data_t', True)
    return entry, vault, marker, copies


def confined_transfer_process(pid):
    assert Path(f'/proc/{pid}/attr/current').read_text().split(':')[2] == 'liner_notes_transfer_t'


def transfer_probe(fixture, port, marker, binaries, *, observe=confined_transfer_process):
    from test_telnet import Client
    from test_zmodem_menu import transfer
    from test_door_resize import gone
    data = fixture.path / 'client-files'
    data.mkdir()
    output = fixture.path / 'client-output'
    output.mkdir()
    payload = bytes(range(256))*257
    (data/'payload.bin').write_bytes(payload)
    c = Client(port)
    try:
        c.until()
        time.sleep(1.1)
        c.cmd('LOGIN alice',b'> ')
        c.cmd('selinux-only-fixture-password')
        last = 0
        for command, program, arguments, directory, receipt in (
            ('ZUPLOAD fixture upload', binaries[1], ['--','payload.bin'], data, b'published'),
            ('ZDOWNLOAD 1', binaries[0], ['--restricted'], output, b'sent (')):
            c.cmd(command, b'1 GiB.\r\n')
            for _ in range(100):
                if marker.exists():
                    value = marker.read_text()
                    if value.isdigit() and int(value) != last:
                        last = int(value)
                        break
                time.sleep(.02)
            else:
                raise AssertionError('Confined transfer helper never reached its denial probe')
            observe(last)
            try:
                code, text, error = transfer(c, str(program), arguments, directory)
            except (AssertionError, TimeoutError) as failure:
                raise AssertionError('Native acceptance transfer: ' + command) from failure
            assert code == 0 and receipt in text and b'failed or cancelled' not in text, (code, text, error)
            gone(last)
        assert (output/'payload.bin').read_bytes() == payload
        fixture.results.append({'transfer':'real ZMODEM upload/download byte equality',
                                'process_observer': observe.__name__})
    finally:
        c.close()


def confined_admin_process(pid):
    assert Path(f'/proc/{pid}/attr/current').read_text().split(':')[2] == 'liner_notes_admin_t'


def confined_admin_launch(fixture, command):
    # Uses the owning uid, with no capability/DAC override granted to a daemon.
    return subprocess.Popen(command, user=fixture.account.pw_uid,
                            group=fixture.account.pw_gid, extra_groups=[], cwd=fixture.path,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def admin_command(fixture, command, *, observe, launch, timeout):
    process = launch(fixture, [str(value) for value in command])
    try:
        pid = process.stdout.readline().strip()
        assert pid.isdigit(), ('admin process did not report its PID', pid)
        observe(int(pid))
        output, error = process.communicate(timeout=timeout)
        assert process.returncode == 0, (process.returncode, output, error)
        return output
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()


def admin_probe(fixture, vault, *, observe=confined_admin_process, launch=confined_admin_launch):
    binary = fixture.path/'vaultctl'
    shutil.copyfile(ROOT/'vaultctl',binary)
    binary.chmod(0o755)
    fixture.label(binary,'liner_notes_admin_exec_t')
    entry = fixture.text('admin-entry', '#!/usr/bin/python3 -B\n'
        'import os,time\n'
        'print(os.getpid(),flush=True)\n'
        'time.sleep(1)\n'
        f'os.execv({str(binary)!r},[{str(binary)!r},"list",{str(vault)!r}])\n',
        'liner_notes_admin_exec_t',True)
    output = admin_command(fixture, [entry], observe=observe, launch=launch, timeout=15)
    assert 'payload.bin' in output, ('successful list lacked exported upload', output)
    fixture.results.append({'admin':'offline vault list from logical SQL export',
                            'process_observer':observe.__name__})


def maintenance_probe(fixture, core, *, observe=confined_admin_process, launch=confined_admin_launch):
    backup = fixture.directory('backups', 'liner_notes_backup_t')
    converter = fixture.path / 'dbconvert'
    shutil.copyfile(ROOT / 'dbconvert', converter)
    converter.chmod(0o755)
    fixture.label(converter, 'liner_notes_admin_exec_t')
    scripts = fixture.path / 'transfer-scripts'
    entry = fixture.text('maintenance-entry', '#!/usr/bin/python3 -B\n'
        'import os,sys,time,runpy\n'
        'print(os.getpid(),flush=True)\ntime.sleep(1)\n'
        f'sys.path.insert(0,{str(scripts)!r})\n'
        f'runpy.run_path({str(scripts / "maintenance.py")!r},run_name="__main__")\n',
        'liner_notes_admin_exec_t', True)
    archive, restored, exported = backup/'instance.tar.gz', backup/'restored', backup/'exported'
    for arguments in (
        ['db-backup', '--root', core, '--database', core/'notes.sqlite',
         '--board', 'notes', '--members', 'members', archive],
        ['verify', archive],
        ['db-restore', archive, restored, '--database', 'notes.sqlite', '--tools-dir', fixture.path],
        ['db-export', '--root', core, '--database', core/'notes.sqlite', '--board', 'notes', exported],
    ):
        admin_command(fixture, [entry, *arguments], observe=observe, launch=launch, timeout=60)
    # vaultctl reads portable index.dat, not the runtime's authoritative SQL
    # stream. Inspect only the new private export; never the stale original.
    admin_probe(fixture, exported/'vault', observe=observe, launch=launch)
    # A restored copy is explicitly relabelled for its future BBS role.
    fixture.label(restored, 'liner_notes_data_t', True)
    fixture.results.append({'admin':'logical backup, verification, export and structured restore',
                            'process_observer':observe.__name__})
    return restored


def core_arguments(arguments, bootstrap=False):
    """Enable the link allowlist only after its fixture member exists."""
    result = list(arguments)
    if bootstrap:
        offset = result.index('--link-accounts')
        del result[offset:offset+2]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--designated-ci-vm', action='store_true', required=True)
    parser.parse_args()
    fixture = Fixture()
    try:
        core = fixture.directory('core', 'liner_notes_data_t')
        games = fixture.directory('games', 'liner_notes_door_data_t')
        executable = fixture.path / 'runtime'
        shutil.copyfile(ROOT / 'runtime', executable)
        executable.chmod(0o755)
        fixture.label(executable, 'liner_notes_exec_t')
        # Same DAC uid for both domains deliberately proves that MAC denies it.
        secret = core / 'fixture-secret'
        secret.write_text('synthetic data, not a real credential')
        secret.chmod(0o644)
        fixture.label(secret, 'liner_notes_data_t')
        helper, vault, marker, binaries = transfer_fixture(fixture, core, secret)
        game = fixture.text('door', '#!/usr/bin/python3 -B\n'
            'import os,sys,tty\n'
            'from pathlib import Path\n'
            'tty.setraw(0)\n'
            'try:\n'
            f'    Path({str(secret)!r}).read_text()\n'
            'except PermissionError:\n'
            '    print("CONFINED",os.getpid(),"DENIED\\r\\nKEY> ",end="",flush=True)\n'
            'else:\n'
            '    raise RuntimeError("Door read BBS data")\n'
            'key=os.read(0,1)\n'
            'sys.exit(42 if key==b"x" else 0)\n', 'liner_notes_door_exec_t', True)
        config = fixture.text('doors.conf', f'1|0|30|{game}|{games}|MAC probe|dorinfo1:1\n', 'liner_notes_etc_t')
        accounts = fixture.text('link-accounts', '1 3\n', 'liner_notes_etc_t')
        port = fixture.port('liner_notes_port_t')
        args = ['--telnet', '--menu', '--lang', 'en', '--doors', str(config),
                '--link-accounts', str(accounts),
                '--vault-dir', str(vault), '--zmodem-helper', str(helper),
                '--database-structured', str(core/'notes.sqlite'), str(port), str(core/'notes'), str(core/'members')]
        for iteration in range(3):
            # A link allowlist may only reference existing members. Bootstrap
            # the fresh database first, then enable the fixed test account.
            current = core_arguments(args, bootstrap=iteration == 0)
            unit = fixture.start('core'+str(iteration), executable, current, core, 'liner_notes_t')
            # Readiness retries are safe: only connection attempts precede writes.
            fixture.ready(port)
            asyncio.run(bbs_probe(port, iteration == 0))
            if iteration == 1:
                door_probe(port)
                web_fixture(fixture, port, secret)
                asyncio.run(link_probe(fixture, port))
                transfer_probe(fixture, port, marker, binaries)
            fixture.stop(unit)
        restored = maintenance_probe(fixture, core)
        restored_args = [str(restored) + value[len(str(core)):] if value.startswith(str(core)+'/') else value
                         for value in args]
        # Its parent is an administrator-only backup directory. Move the verified
        # copy to the fixture root before admitting the BBS service to it.
        recovered = fixture.path / 'recovered'
        restored.rename(recovered)
        restored_args = [str(recovered) + value[len(str(restored)):] if value.startswith(str(restored)+'/') else value
                         for value in restored_args]
        unit = fixture.start('recovered', executable, restored_args, recovered, 'liner_notes_t')
        fixture.ready(port)
        asyncio.run(bbs_probe(port, False))
        fixture.stop(unit)
        fixture.results.append({'bbs': 'Telnet/articles/mail/SQLite restart', 'door': 'PTY/normal/abnormal/disconnect/MAC denial'})
        assert run('getenforce').stdout.strip() == 'Enforcing'
    finally:
        fixture.close()


if __name__ == '__main__':
    main()
