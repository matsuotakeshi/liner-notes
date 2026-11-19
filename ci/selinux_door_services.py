"""Reviewed-main-only adapter: run the existing Door suite in real systemd domains.

No installed service or account is changed. Units have unique fixture names;
only their own runtime unit files are removed. The parent SELinux fixture owns
policy loading, the VM gate, state record, audit filtering and final cleanup.
"""
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def argv(arguments):
    # systemd expands specifiers and variables even without an OS shell.
    return ' '.join(json.dumps(str(value).replace('%', '%%').replace('$', '$$'))
                    for value in arguments)


class Process:
    def __init__(self, backend, role, text, domain):
        self.backend = backend
        self.name = backend.fixture.path.name + '-door-' + role + '-' + str(len(backend.units))
        self.file = Path('/run/systemd/system') / (self.name + '.service')
        self.content = text
        self.pid = None
        self.returncode = None
        self.stopper = None
        self.stderr_cache = None
        if backend.command('systemctl', 'show', self.name, '-p', 'LoadState', '--value').stdout.strip() != 'not-found':
            raise ValueError('Refuse existing unit')
        with self.file.open('x', encoding='utf8') as stream:
            stream.write(text)
        self.file.chmod(0o644)
        backend.units.append(self)
        backend.fixture.units.append(self.name)
        backend.fixture.save_record()
        backend.command('systemctl', 'daemon-reload')
        backend.command('systemctl', 'start', self.name)
        for _ in range(60):
            fields = self.state()
            pid = int(fields.get('MainPID', '0'))
            if pid:
                try:
                    context = Path('/proc', str(pid), 'attr/current').read_text().strip('\x00\n')
                    if context.split(':')[2] == domain:
                        self.pid = pid
                        backend.fixture.results.append({'door_service': role, 'domain': domain})
                        return
                except FileNotFoundError:
                    pass
            if fields.get('ActiveState') == 'failed':
                break
            time.sleep(.1)
        raise AssertionError(('Door service domain/startup', role, self.stderr.read(4096)))

    def state(self):
        result = self.backend.command('systemctl', 'show', self.name,
                                      '-p', 'MainPID', '-p', 'ActiveState', '-p', 'ExecMainStatus')
        return dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)

    @property
    def stderr(self):
        if self.stderr_cache is None:
            output = self.backend.command('journalctl', '--no-pager', '-u', self.name, '-n', '40').stdout
            self.stderr_cache = io.BytesIO(output.encode())
        return self.stderr_cache

    def poll(self):
        if self.returncode is None:
            fields = self.state()
            if fields.get('ActiveState') in ('inactive', 'failed') and fields.get('MainPID') == '0':
                self.returncode = int(fields.get('ExecMainStatus', '0'))
        return self.returncode

    def terminate(self):
        if self.returncode is None and self.stopper is None:
            self.stopper = subprocess.Popen(['systemctl', 'stop', self.name], stdout=subprocess.DEVNULL,
                                            stderr=subprocess.PIPE)

    def kill(self):
        # This unit generation has a unique name. Target its main process only;
        # leave isolation workers alive to perform their own revocation/cleanup.
        self.backend.command('systemctl', 'kill', '--kill-whom=main', '--signal=KILL', self.name)

    def wait(self, timeout):
        until = time.monotonic() + timeout
        while self.poll() is None:
            if time.monotonic() >= until:
                raise subprocess.TimeoutExpired(self.name, timeout)
            time.sleep(.05)
        if self.stopper:
            _, errors = self.stopper.communicate(timeout=max(.1, until-time.monotonic()))
            assert self.stopper.returncode == 0, ('systemd stop', errors)
        return self.returncode

    def join(self, timeout):
        try:
            self.wait(timeout)
        except subprocess.TimeoutExpired:
            pass

    def is_alive(self):
        return self.poll() is None


class Backend:
    def __init__(self, fixture, command):
        import grp
        import pwd
        from test_door_broker_linux import BBS, BROKER, GAME, SLOT1, SLOT2
        # Native VM is shared infrastructure, unlike a disposable container.
        # Refuse even an inactive preexisting account/group at a fixture ID.
        for uid in (BBS, BROKER, GAME, SLOT1, SLOT2):
            for lookup in (pwd.getpwuid, grp.getgrgid):
                try:
                    lookup(uid)
                except KeyError:
                    continue
                raise ValueError('Door fixture UID/GID is already allocated')
        self.fixture, self.command = fixture, command
        self.path = fixture.path
        self.units = []
        fixture.door_slot_uids = [SLOT1, SLOT2]
        fixture.save_record()

    def label(self, path, kind, recursive=False):
        self.fixture.label(path, 'liner_notes_' + kind + '_t', recursive)

    def prepare(self, base, broker, isolation, slots):
        from test_door_broker_linux import BBS, BROKER, GAME
        self.base, self.configs = base, {'broker': broker, 'isolation': isolation}
        self.label(base, 'ro', True)
        for name, kind in [('data','data'), ('broker','broker_run'), ('isolation','isolation_run'),
                           ('state','isolation_state'), ('server','consumer_run')]:
            self.label(base/name, kind, True)
        self.label(base/'runtime', 'exec')
        self.label(base/'game', 'door_exec')
        # Unlike private BBS files (0700 parent), this synthetic secret is
        # deliberately DAC-readable. Rejection therefore proves MAC isolation.
        secret = base/'mac-secret'
        secret.write_text('synthetic MAC probe'); secret.chmod(0o644)
        self.label(secret, 'data')
        game = base/'game'
        probe = ("assert Path('/proc/self/attr/current').read_text().split(':')[2]=='liner_notes_door_t'\n"
                 +f'try:Path({str(secret)!r}).read_bytes()\n'
                 +'except PermissionError:pass\n'
                 +"else:raise AssertionError('MAC allowed private BBS data')\n")
        game.write_text(game.read_text().replace('tty.setraw(0)',probe+'tty.setraw(0)'))
        self.label(broker, 'broker_etc')
        self.label(isolation, 'isolation_etc')
        self.label(base/'doors.conf', 'etc')
        for slot in slots:
            self.label(slot['cwd'], 'door_data')
        # The consumer creates SQLite files inside its own run directory; label
        # its dedicated data files via a directory type transition in policy.
        for role in ('broker', 'isolation'):
            entry = base/('entry-'+role)
            script = (ROOT/'linux'/('door-'+role+'.in')).read_text()
            script = script.replace('@PYTHON@', sys.executable).replace(
                '@PREFIX@/share/liner-notes/scripts', str(base/'scripts'))
            entry.write_text(script); entry.chmod(0o755)
            self.label(entry, role+'_exec')
        self.ids = {'bbs': BBS, 'broker': BROKER, 'consumer': GAME, 'isolation': 0}

    def unit(self, role, command, domain, *, directory=None, output='journal'):
        uid = self.ids[role]
        text = ('[Service]\nType=simple\nUser='+str(uid)+'\nGroup='+str(uid)+
                '\nWorkingDirectory='+str(directory or self.base)+
                '\nNoNewPrivileges=yes\nPrivateTmp=yes\nProtectHome=yes\nProtectSystem=strict'+
                '\nReadWritePaths='+str(self.base)+'\nRestart=no\nRuntimeMaxSec=300\nLimitCORE=0\nUMask=0077'+
                '\nStandardOutput='+output+'\nStandardError=journal\nExecStart='+argv(command)+'\n')
        return Process(self, role, text, domain)

    def start_service(self, role):
        # Preserve the shipped hardening, preflight and termination settings.
        # Translate only identities and instance paths for this isolated copy.
        template = (ROOT/'linux'/('liner-notes-door-'+role+'.service')).read_text()
        template = template.replace('@PREFIX@/libexec/liner-notes/door-'+role, str(self.base/('entry-'+role)))
        template = template.replace('/etc/liner-notes/door-'+role+'.json', str(self.configs[role]))
        lines = []
        for line in template.splitlines():
            if line.startswith(('Requires=', 'After=', 'Before=', 'WantedBy=', 'RuntimeDirectory=',
                                'RuntimeDirectoryMode=', 'StateDirectory=', 'StateDirectoryMode=', 'ReadWritePaths=')):
                continue
            if line.startswith('User='):line='User='+str(self.ids[role])
            if line.startswith('Group='):line='Group='+str(self.ids[role])
            if line.startswith('SupplementaryGroups='):line='SupplementaryGroups='+str(self.ids['consumer'])
            if line == '[Install]':continue
            lines.append(line)
        lines.append('ReadWritePaths='+str(self.base))
        lines.append('RuntimeMaxSec=300')
        process = Process(self, role, '\n'.join(lines)+'\n', 'liner_notes_'+role+'_t')
        from door_broker import check_ready
        check_ready(json.loads(self.configs[role].read_text())['socket'], self.ids[role])
        return process

    def start_game(self, socket, broker, scripts):
        entry = self.base/'consumer-entry'
        # Copy only fixture code, never import code from a writable game path.
        for name in ('test_door_broker_linux.py','test_board.py','test_runtime.py','test_telnet.py','door_rc_process.py'):
            import shutil
            shutil.copyfile(ROOT/'tests'/name, scripts/name); (scripts/name).chmod(0o644)
            self.label(scripts/name, 'ro')
        entry.write_text('#!'+sys.executable+'\nimport sys\nfrom pathlib import Path\n'
            +'sys.path.insert(0,'+repr(str(scripts))+')\n'
            +'from test_door_broker_linux import game_server\n'
            +f'game_server(Path({str(socket)!r}),Path({str(broker)!r}),Path({str(scripts)!r}),False)\n')
        entry.chmod(0o755); self.label(entry, 'consumer_exec')
        return self.unit('consumer', [entry], 'liner_notes_consumer_t')

    def server(self, binary, store, **kw):
        # Same real runtime/clients/scenarios as the hosted fixture; only its
        # launch transport changes from Popen to a fresh confined systemd unit.
        port = self.fixture.port('liner_notes_port_t')
        args = [binary, *kw['options'], '--menu', '--telnet', '--lang', kw['language'],
                str(port), str(store), str(kw['members'])]
        process = self.unit('bbs', args, 'liner_notes_t', directory=Path(store).parent)
        self.fixture.ready(port)
        class BBS:
            def close(self):
                process.terminate(); process.wait(10)
                assert process.returncode == 0, 'BBS service exited abnormally'
        bbs = BBS(); bbs.port = port
        return bbs

    def close(self):
        # Never erase another file, reset BUSY or kill every process of an UID.
        # The original fixture has already checked/reclaimed its owned slots.
        for process in reversed(self.units):
            process.terminate()
            process.wait(15)
            assert process.file.read_text() == process.content, 'fixture unit changed'
            process.file.unlink()
        self.command('systemctl', 'daemon-reload')


def acceptance(fixture, command):
    from test_door_broker_linux import run
    backend = Backend(fixture, command)
    try:
        run(str(ROOT/'runtime'), backend=backend)
        fixture.results.append({'door_broker':'systemd + Enforcing identity/lifecycle/revocation/PTY/persistence/crash refusal'})
    finally:
        backend.close()
