"""Reviewed-main-only adapter: run the existing Door suite in real systemd domains.

No existing service or account is changed. Temporary non-login fixture identities
are owned by this run and removed only after all fixture processes exit.
Units have unique names;
only their own runtime unit files are removed. The parent SELinux fixture owns
policy loading, the VM gate, state record, audit filtering and final cleanup.
"""
import errno
import hashlib
import io
import json
import os
import stat
from pathlib import Path
import subprocess
import sys
import time


def private_diagnostics(path):
    """Export only the isolation service's bounded, allowlisted diagnostic fields."""
    events = {'worker-accepted', 'worker-reaped', 'admission-rejected',
              'admission-unavailable', 'worker-exit', 'door-exit', 'launch-rejected',
              'launch-failed', 'cleanup-failed', 'worker-failed', 'slot-permission-error'}
    labels = {'phase': {'request', 'slot', 'scope', 'broker', 'ticket-pipe', 'dropfile',
                       'pty', 'launch', 'handoff', 'running', 'revoke', 'descriptors',
                       'scope-stop', 'dropfile-remove', 'slot-release', 'connection-close',
                       'setup', 'supervise'},
              'reason': {'peer', 'capacity', 'credentials', 'fork'},
              'function': {'slot_acquire', 'processes'}}
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0)
    fd = os.open(path, flags)
    with os.fdopen(fd, 'rb') as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o077 or info.st_nlink != 1:
            raise ValueError('unsafe diagnostic file')
        content = stream.read(16385)
    output = []
    for line in content[:16384].splitlines():
        try:
            item = json.loads(line)
        except (ValueError, UnicodeDecodeError):
            continue
        if not isinstance(item, dict) or not isinstance(item.get('event'), str) or item['event'] not in events:
            continue
        clean = {'event': item['event']}
        for key in ('worker', 'active', 'status', 'errno', 'line'):
            if type(item.get(key)) is int: clean[key] = item[key]
        for key, allowed in labels.items():
            if isinstance(item.get(key), str) and item[key] in allowed: clean[key] = item[key]
        # Exception names are deliberately omitted: no arbitrary string fields.
        output.append(json.dumps(clean))
    if len(content) > 16384: output.append('DIAGNOSTIC_TRUNCATED')
    return ('\n'.join(output)+'\n').encode() if output else b''


def probe_delivery(command, name, pid, since):
    """Check only the fixed marker, including records missing unit attribution."""
    found = {'unit': False, 'pid': False}
    queries = {'unit': ['-u', name], 'pid': ['_PID='+str(pid)]}
    for attempt in range(3):
        for kind, match in queries.items():
            if found[kind]: continue
            result = command('journalctl', '--no-pager', '-b', '--since', since,
                             '-n', '40', '-o', 'json', *match, check=False)
            if result.returncode: continue
            for line in result.stdout.splitlines():
                try: record = json.loads(line)
                except ValueError: continue
                if isinstance(record, dict) and record.get('MESSAGE') == 'LINER_DOOR_JOURNAL_PROBE':
                    if kind == 'pid' and record.get('_PID') != str(pid): continue
                    found[kind] = True
        if any(found.values()): break
        if attempt < 2: time.sleep(.1)
    return found

ROOT = Path(__file__).resolve().parents[1]


def argv(arguments):
    # systemd expands specifiers and variables even without an OS shell.
    return ' '.join(json.dumps(str(value).replace('%', '%%').replace('$', '$$'))
                    for value in arguments)


class Accounts:
    """NSS identities for every non-root systemd service, never shared users."""
    def __init__(self, fixture, command, broker, game, uids, *, bbs):
        import grp
        import pwd
        self.pwd, self.grp = pwd, grp
        self.fixture, self.command = fixture, command
        self.broker, self.game, self.uids = broker, game, tuple(uids)
        suffix = hashlib.sha256(str(fixture.path).encode()).hexdigest()[:12]
        self.user = 'lnci_b_' + suffix
        self.users = [(self.user, broker), ('lnci_c_' + suffix, game), ('lnci_s_' + suffix, bbs)]
        self.groups = [('lnci_bg_' + suffix, broker), ('lnci_g_' + suffix, game),
                       ('lnci_sg_' + suffix, bbs)]
        self.created = []

    def record(self):
        self.fixture.door_accounts = list(self.created)
        self.fixture.save_record()

    def idle(self):
        # Examine all real/effective/saved/fs IDs, including root workers that
        # have not completely dropped a fixture identity. Races fail closed.
        for path in Path('/proc').glob('[0-9]*/status'):
            try:
                lines = path.read_text().splitlines()
            except FileNotFoundError:
                continue
            for line in lines:
                if line.startswith(('Uid:', 'Gid:', 'Groups:')):
                    if set(map(int, line.split()[1:])) & set(self.uids):
                        raise ValueError('Fixture identity still has a live process')

    @staticmethod
    def absent(lookup, key):
        try:
            lookup(key)
        except KeyError:
            return
        raise ValueError('Fixture identity already allocated')

    def create(self):
        self.idle()
        # Check every name and number before making any change. Shadow tools
        # also refuse collisions atomically; never use non-unique/force flags.
        for uid in self.uids:
            self.absent(self.pwd.getpwuid, uid)
            self.absent(self.grp.getgrgid, uid)
        for name, uid in self.users:
            self.absent(self.pwd.getpwnam, name)
        for name, gid in self.groups:
            self.absent(self.grp.getgrnam, name)
        self.fixture.door_account_plan = [
            {'kind': 'group', 'name': name, 'id': gid} for name, gid in self.groups
        ] + [{'kind': 'user', 'name': name, 'id': uid} for name, uid in self.users]
        self.record()  # recovery intent survives interruption during account tools
        for name, gid in self.groups:
            self.command('/usr/sbin/groupadd', '--gid', str(gid), name)
            self.created.append({'kind': 'group', 'name': name, 'id': gid})
            self.record()
        for name, uid in self.users:
            self.command('/usr/sbin/useradd', '--uid', str(uid),
                         '--gid', str(uid), '--no-user-group', '--no-create-home',
                         '--no-log-init', '-K', 'SUB_UID_COUNT=0', '-K', 'SUB_GID_COUNT=0', '--home-dir', '/nonexistent', '--shell', '/usr/sbin/nologin',
                         '--password', '!', name)
            self.created.append({'kind': 'user', 'name': name, 'id': uid})
            self.record()

    def close(self):
        if not self.created:
            return
        self.idle()
        # Validate the entire set before deleting anything. Preserve changed
        # identities for explicit recovery instead of deleting foreign state.
        owned_users = {(entry['name'], entry['id']) for entry in self.created if entry['kind'] == 'user'}
        for entry in reversed(self.created):
            name, uid = entry['name'], entry['id']
            if entry['kind'] == 'user':
                user = self.pwd.getpwnam(name)
                if (user.pw_uid != uid or user.pw_gid != uid or
                        user.pw_dir != '/nonexistent' or user.pw_shell != '/usr/sbin/nologin' or
                        self.pwd.getpwuid(uid).pw_name != name):
                    raise ValueError('Fixture account changed; retain for recovery')
            else:
                group = self.grp.getgrnam(name)
                if group.gr_gid != uid or group.gr_mem or self.grp.getgrgid(uid).gr_name != name:
                    raise ValueError('Fixture group changed; retain for recovery')
                for user in self.pwd.getpwall():
                    if user.pw_gid == uid and (user.pw_name, user.pw_uid) not in owned_users:
                        raise ValueError('Foreign user references fixture group')
        for entry in reversed(self.created.copy()):
            tool = '/usr/sbin/userdel' if entry['kind'] == 'user' else '/usr/sbin/groupdel'
            self.command(tool, entry['name'])  # no force, no home/data removal
            self.created.remove(entry)
            self.record()


class Process:
    def __init__(self, backend, role, text, domain, *, diagnostic=None):
        self.backend = backend
        self.started_at = time.strftime('%Y-%m-%d %H:%M:%S')
        self.name = backend.fixture.path.name + '-door-' + role + '-' + str(len(backend.units))
        self.file = Path('/run/systemd/system') / (self.name + '.service')
        self.content = text
        self.pid = None
        self.returncode = None
        self.stopper = None
        self.stderr_cache = None
        self.private_log = diagnostic
        self.private_cache = None
        if backend.command('systemctl', 'show', self.name, '-p', 'LoadState', '--value').stdout.strip() != 'not-found':
            raise ValueError('Refuse existing unit')
        with self.file.open('x', encoding='utf8') as stream:
            stream.write(text)
        self.file.chmod(0o644)
        backend.units.append(self)
        backend.fixture.units.append(self.name)
        backend.fixture.save_record()
        backend.command('systemctl', 'daemon-reload')
        try:
            backend.command('systemctl', 'start', self.name)
        except Exception:
            self.diagnostics()
            raise
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
        self.diagnostics()
        raise AssertionError(('Door service domain/startup', role))

    def diagnostics(self):
        # Capture before unit removal/daemon-reload, even if ExecStartPre fails.
        # Only this unique fixture unit is queried; never export whole journals.
        try:
            self.capture_private()
            result = self.backend.command('journalctl', '--no-pager', '-u', self.name,
                                          '-n', '40', '-o', 'short-iso', check=False)
            self.stderr_cache = io.BytesIO(result.stdout.encode()[-16384:])
            print('DOOR_UNIT_JOURNAL ' + self.name + '\n' + self.stderr_cache.getvalue().decode(errors='replace'), flush=True)
            if result.returncode:
                print('DOOR_UNIT_JOURNAL unavailable (exit ' + str(result.returncode) + ')', flush=True)
        except Exception as error:
            # Keep the original startup/cleanup exception, not a diagnostic error.
            print('DOOR_UNIT_JOURNAL unavailable: ' + type(error).__name__, flush=True)

    def capture_private(self):
        if getattr(self, 'private_log', None) is not None and self.private_cache is None:
            try:
                self.private_cache = private_diagnostics(self.private_log)
                print('DOOR_PRIVATE_DIAGNOSTICS ' + self.name + '\n' + self.private_cache.decode(), flush=True)
            except (OSError, ValueError) as error:
                self.private_cache = b''
                print('DOOR_PRIVATE_DIAGNOSTICS unavailable: ' + type(error).__name__, flush=True)

    def state(self):
        result = self.backend.command('systemctl', 'show', self.name,
                                      '-p', 'MainPID', '-p', 'ActiveState', '-p', 'ExecMainStatus')
        return dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)

    @property
    def stderr(self):
        self.capture_private()
        if getattr(self, 'private_cache', None) is not None:
            return io.BytesIO(self.private_cache)
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


def wait_ready(process, endpoint, uid, timeout=5):
    """Type=simple/process-label success precedes socket creation/listen."""
    from door_broker import check_ready
    deadline = time.monotonic() + timeout
    try:
        while True:
            if process.poll() is not None:
                raise RuntimeError('Door service exited before socket readiness')
            try:
                # Keep the existing socket-type/owner and kernel peer-UID checks.
                # No ticket or game operation is sent by this readiness probe.
                check_ready(endpoint, uid)
                return
            except OSError as error:
                if error.errno not in (errno.ENOENT, errno.ECONNREFUSED):
                    raise
                if time.monotonic() >= deadline:
                    raise TimeoutError('Door service socket readiness deadline expired') from error
                time.sleep(.1)
    except Exception:
        process.diagnostics()
        raise


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
        self.accounts = Accounts(fixture, command, BROKER, GAME, (BBS, BROKER, GAME, SLOT1, SLOT2), bbs=BBS)
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
                 +"caps={line.split(':')[0]:line.split(':')[1].strip() for line in Path('/proc/self/status').read_text().splitlines() if line.startswith(('CapEff:','CapPrm:','CapAmb:'))}\n"
                 +"assert set(caps)=={'CapEff','CapPrm','CapAmb'} and all(int(v,16)==0 for v in caps.values()),caps\n"
                 +"assert os.getuid()==os.geteuid()!=0\n"
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
        capture = base/'scripts'/'selinux_isolation_entry.py'
        capture.write_bytes((ROOT/'ci'/'selinux_isolation_entry.py').read_bytes())
        capture.chmod(0o644); self.label(capture, 'ro')
        entry = base/'entry-isolation-capture'
        entry.write_text((base/'entry-isolation').read_text().replace(
            'door_isolation.py', 'selinux_isolation_entry.py'))
        entry.chmod(0o755); self.label(entry, 'isolation_exec')
        self.probe_journal()

    def probe_journal(self):
        # Same fixed shell/Python entry and SELinux transition as isolation.
        # Keep journal output here even though the following diagnostic launch
        # saves stderr privately. A failed journal probe still fails acceptance.
        script = self.base/'scripts'/'selinux_journal_probe.py'
        script.write_bytes((ROOT/'ci'/'selinux_journal_probe.py').read_bytes())
        script.chmod(0o644); self.label(script, 'ro')
        entry = self.base/'entry-journal-probe'
        entry.write_text((self.base/'entry-isolation').read_text().replace(
            'door_isolation.py', 'selinux_journal_probe.py'))
        entry.chmod(0o755); self.label(entry, 'isolation_exec')
        result = self.base/'state'/'journal-probe.json'
        probe = self.unit('isolation', [entry, '--result', result], 'liner_notes_isolation_t')
        try:
            until = time.monotonic() + 5
            while not result.exists() and probe.poll() is None and time.monotonic() < until:
                time.sleep(.05)
        finally:
            probe.terminate(); probe.wait(10)
        assert probe.returncode == 0, 'journal probe exited abnormally'
        record = json.loads(result.read_text())
        if set(record) != {'bytes', 'errno'} or any(type(x) is not int for x in record.values()):
            raise ValueError('invalid journal probe result')
        probe.diagnostics()
        delivery = probe_delivery(self.command, probe.name, probe.pid, probe.started_at)
        self.journal_ok = record == {'bytes': 25, 'errno': 0} and any(delivery.values())
        self.fixture.results.append({'door_journal': record, 'delivery': delivery, 'verified': self.journal_ok})
        print('DOOR_JOURNAL_PROBE ' + json.dumps({'write': record, 'delivery': delivery, 'verified': self.journal_ok}), flush=True)

    def assert_journal(self):
        assert self.journal_ok, 'isolation journal write/delivery failed (see DOOR_JOURNAL_PROBE)'

    def diagnostic_path(self):
        path = self.base/'state'/('diagnostic-'+str(len(self.units))+'.jsonl')
        fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        os.close(fd)
        self.label(path, 'isolation_state')
        return path

    def unit(self, role, command, domain, *, directory=None, output='journal'):
        uid = self.ids[role]
        text = ('[Service]\nType=simple\nUser='+str(uid)+'\nGroup='+str(uid)+
                ('\nEnvironment=LINER_DOOR_DIAGNOSTICS=1' if role == 'bbs' else '')+
                '\nWorkingDirectory='+str(directory or self.base)+
                '\nNoNewPrivileges=yes\nPrivateTmp=yes\nProtectHome=yes\nProtectSystem=strict'+
                '\nReadWritePaths='+str(self.base)+'\nRestart=no\nRuntimeMaxSec=300\nLimitCORE=0\nUMask=0077'+
                '\nStandardOutput='+output+'\nStandardError=journal\nExecStart='+argv(command)+'\n')
        return Process(self, role, text, domain)

    def start_service(self, role):
        # Preserve the shipped hardening, preflight and termination settings.
        # Translate only identities and instance paths for this isolated copy.
        diagnostic = self.diagnostic_path() if role == 'isolation' else None
        entry = (argv([self.base/'entry-isolation-capture', '--diagnostic', diagnostic, '--'])
                 if diagnostic is not None else str(self.base/('entry-'+role)))
        template = (ROOT/'linux'/('liner-notes-door-'+role+'.service')).read_text()
        template = template.replace('@PREFIX@/libexec/liner-notes/door-'+role, entry)
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
        if role=='isolation':lines.append('Environment=LINER_DOOR_DIAGNOSTICS=1')
        process = Process(self, role, '\n'.join(lines)+'\n', 'liner_notes_'+role+'_t', diagnostic=diagnostic)
        wait_ready(process, json.loads(self.configs[role].read_text())['socket'], self.ids[role])
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
            try:
                process.terminate()
                process.wait(15)
            finally:
                process.diagnostics()
            assert process.file.read_text() == process.content, 'fixture unit changed'
            process.file.unlink()
        self.command('systemctl', 'daemon-reload')
        self.accounts.close()


def acceptance(fixture, command):
    from test_door_broker_linux import run
    backend = Backend(fixture, command)
    try:
        backend.accounts.create()
        run(str(ROOT/'runtime'), backend=backend)
        backend.assert_journal()
        fixture.results.append({'door_broker':'systemd + Enforcing identity/lifecycle/revocation/PTY/persistence/crash refusal'})
    finally:
        backend.close()
