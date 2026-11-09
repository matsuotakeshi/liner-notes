#!/usr/bin/env python3
"""Root-owned, closed-command deployment adapter (not installed automatically).

Reads one signed request from stdin. No command/path/host arguments are accepted.
The dedicated sudo rule must permit this executable without arguments only.
"""
import contextlib
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import sqlite3
import stat
import subprocess
import sys
import tarfile

from deploy_policy import (PACKAGES, PLATFORMS, SERVICES, HEX64, canonical, digest_file, machine_identity,
                           package_commands, public_key_identity, require, service_command,
                           transaction_id, validate_authorization, validate_release, verify)
from deploy_transaction import apply_host, atomic_json

CONFIG = Path('/etc/liner-notes-deploy/host.json')
REQUIRED_ROOTS = {'data', 'config', 'attachments', 'vault', 'spool', 'link', 'web', 'doors', 'binaries', 'old-packages'}
COMMAND_LOG = None
OPTIONAL_ROOTS = {'attachments', 'vault', 'spool', 'link', 'web', 'doors'}


def validate_backup_inventory(profile, platform):
    roots = profile['backup_roots']
    require(set(roots) == REQUIRED_ROOTS and all(isinstance(value, list) for value in roots.values()),
            'Incomplete backup inventory')
    require(all(roots[name] for name in ('data', 'config', 'binaries', 'old-packages')), 'Required backup roots missing')
    features = profile.get('backup_features', {})
    require(set(features) == OPTIONAL_ROOTS and all(type(value) is bool for value in features.values()),
            'Optional backup features require explicit enabled/disabled states')
    for feature in OPTIONAL_ROOTS:
        require(bool(roots[feature]) == features[feature], 'Enabled backup feature has no roots, or disabled feature has roots')
    for feature, service in zip(('web', 'link'), SERVICES[platform][1:]):
        require(service not in profile['services'] or features[feature], 'Enabled service has no corresponding backup roots')


def validate_link_processes(lines, managed, allowed_uids):
    for line in lines.splitlines():
        fields = line.strip().split(None, 1)
        if len(fields) == 2 and re.search(r'(?:^|\s)(?:\S*/)?(?:link_service\.py|liner-notes-link)\s+serve(?:\s|$)', fields[1]):
            uid = int(fields[0])
            require(managed and uid != 0 and uid in allowed_uids,
                    'Running link daemon has no managed stop/backup owner; configure its service first')


def trusted(path):
    path = Path(path)
    require(path.is_absolute(), 'Absolute trusted path required')
    for item in (path, *path.parents):
        info = item.lstat()
        require(not stat.S_ISLNK(info.st_mode) and info.st_uid == 0 and not info.st_mode & 0o022,
                'Deployment configuration/executable must be root-owned and not writable by others')
    return path


def command(argv, *, accepted=(0,), input=None, with_status=False):
    try:
        result = subprocess.run(argv, input=input, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                env={'PATH': '/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin:/usr/local/sbin',
                                     'LC_ALL': 'C', 'HOME': '/root'}, timeout=1800, check=False)
    except subprocess.TimeoutExpired as error:
        result = subprocess.CompletedProcess(argv, -1, error.stdout or b'', (error.stderr or b'') + b'\nRestricted command timed out')
    except OSError:
        result = subprocess.CompletedProcess(argv, -1, b'', b'Restricted command could not start')
    if COMMAND_LOG is not None:
        # Root-local diagnostic only: never return this file in SSH responses or
        # upload it as an Artifact. In particular, stdin may contain secrets and
        # is deliberately not recorded.
        fd = os.open(COMMAND_LOG, os.O_WRONLY | os.O_APPEND | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        with os.fdopen(fd, 'a', encoding='utf8') as stream:
            info = os.fstat(stream.fileno())
            require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and not info.st_mode & 0o077,
                    'Private command log permissions are invalid')
            stream.write(json.dumps({'time': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                     'program': str(argv[0]), 'returncode': result.returncode,
                                     'stdout': result.stdout[-65536:].decode('utf8', errors='replace'),
                                     'stderr': result.stderr[-65536:].decode('utf8', errors='replace')}) + '\n')
            stream.flush()
            os.fsync(stream.fileno())
    require(result.returncode in accepted, 'Restricted host command failed')
    output = result.stdout.decode('utf-8', errors='strict').strip()
    return (result.returncode, output) if with_status else output


class Host:
    def __init__(self, profile):
        global COMMAND_LOG
        self.profile = profile
        self.target = profile['target']
        require(self.target in PLATFORMS, 'Unknown target')
        self.platform = PLATFORMS[self.target]
        self.root = trusted(profile['state_directory'])
        require(self.root.is_dir() and not self.root.stat().st_mode & 0o077, 'Private deployment state directory required')
        COMMAND_LOG = self.root / 'commands.log'
        require(set(profile['services']) <= set(SERVICES[self.platform]) and SERVICES[self.platform][0] in profile['services'], 'Unexpected service')
        require(profile['services'] == [s for s in SERVICES[self.platform] if s in profile['services']], 'Services must follow core/web/link order without duplicates')
        validate_backup_inventory(profile, self.platform)
        # Root-maintained probes are declarative local TCP/SQLite checks, not shell hooks.
        require({'login', 'article', 'mail'} <= set(profile['probes']), 'Login/article/mail health probes are mandatory')
        for feature, service in zip(('web', 'link'), SERVICES[self.platform][1:]):
            require(service not in profile['services'] or feature in profile['probes'], 'Enabled service has no health probe')
        require(not profile['backup_roots']['doors'] or 'door' in profile['probes'], 'Registered Doors require a health probe')
        require(profile.get('quiescence_users'), 'Dedicated service accounts must be checked for remaining workers')
        required_users = {'liner_notes'}
        for user, service in zip(('liner_notes_web', 'liner_notes_link'), SERVICES[self.platform][1:]):
            if service in profile['services']:
                required_users.add(user)
        require(required_users <= set(profile['quiescence_users']), 'Known service owner missing from quiescence inventory')

    def check_service_inventory(self):
        """Only the three known BBS services; never operate unrelated services."""
        import pwd
        ps = '/bin/ps' if self.platform == 'freebsd15' else '/usr/bin/ps'
        validate_link_processes(command([ps, '-ax', '-o', 'uid=,command=']),
                                SERVICES[self.platform][2] in self.profile['services'],
                                {pwd.getpwnam(name).pw_uid for name in self.profile['quiescence_users']})
        for service, user_name in zip(SERVICES[self.platform], ('liner_notes', 'liner_notes_web', 'liner_notes_link')):
            included = service in self.profile['services']
            if self.platform == 'freebsd15':
                rc = Path('/usr/local/etc/rc.d') / service
                if included:
                    trusted(rc)
                    require(rc.is_file(), 'Managed FreeBSD service has no installed rc script')
                if rc.exists() and not included:
                    trusted(rc)
                    status, output = command(service_command(self.platform, service, 'status'), accepted=(0, 1), with_status=True)
                    require(status == 1 and ('is not running' in output or 'is not ready' in output),
                            'Omitted BBS service is active or its state is unknown')
            elif included:
                owner = command(['/usr/bin/systemctl', 'show', '-p', 'User', '--value', service])
                require(owner in self.profile['quiescence_users'] and pwd.getpwnam(owner).pw_uid != 0,
                        'Configured systemd service owner is absent from quiescence inventory')
            else:
                status, output = command(service_command(self.platform, service, 'status'), accepted=(0, 3, 4), with_status=True)
                require(status != 0 and output in ('inactive', 'failed', 'unknown'),
                        'Omitted BBS service is active or its state is unknown')
            if not included:
                try:
                    user = pwd.getpwnam(user_name)
                except KeyError:
                    continue
                pgrep = '/bin/pgrep' if self.platform == 'freebsd15' else '/usr/bin/pgrep'
                require(not command([pgrep, '-u', str(user.pw_uid)], accepted=(0, 1)),
                        'Omitted BBS service account still has running workers')

    @contextlib.contextmanager
    def lock(self):
        import fcntl
        with (self.root / 'transaction.lock').open('a') as stream:
            fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
            for path in self.root.glob('*.json'):
                record = json.loads(path.read_text())
                require(record.get('result') == 'healthy', 'An earlier host transaction requires explicit recovery before any release')
            yield

    def load_state(self, identity):
        path = self.root / (identity + '.json')
        return json.loads(path.read_text()) if path.exists() else None

    def save_state(self, identity, state):
        atomic_json(self.root / (identity + '.json'), state)

    def inventory(self):
        if self.platform == 'freebsd15':
            lines = command(['/usr/local/sbin/pkg', 'query', '%n\t%v\t%o']).splitlines()
        elif self.platform == 'rocky10':
            lines = command(['/usr/bin/rpm', '-qa', '--qf', '%{NAME}\t%{VERSION}-%{RELEASE}\t%{ARCH}\n']).splitlines()
        else:
            lines = command(['/usr/bin/dpkg-query', '-W', '-f=${Package}\t${Version}\t${db:Status-Status}\n']).splitlines()
        result = {}
        for line in lines:
            fields = line.split('\t')
            require(len(fields) == 3, 'Malformed installed package inventory')
            # RPM legitimately keeps several kernel versions. Preserve the
            # complete unrelated inventory without rejecting a normal host.
            key = fields[0] if fields[0] in PACKAGES else '@'.join(fields)
            require(key not in result, 'Ambiguous installed BBS package inventory')
            result[key] = fields[1:]
        return result

    def preflight(self, release):
        identity = transaction_id(release)
        incoming = Path(self.profile['incoming_directory']) / identity
        self.stage = self.root / (identity + '-packages')
        self.stage.mkdir(mode=0o700)
        self.before = self.inventory()
        require(set(self.before).intersection(PACKAGES) <= set(release['platforms'][self.platform]),
                'Release omits an installed BBS component; refuse before stopping services')
        if self.platform == 'freebsd15':
            require(getattr(self, 'tested_previous_version', None) == self.before.get('liner-notes', [None])[0],
                    'Installed FreeBSD package differs from the tested upgrade starting version')
        self.packages = {n: p for n, p in release['platforms'][self.platform].items() if n in self.before}
        require('liner-notes' in self.packages, 'An installed BBS is required; CD does not initialize a host')
        for name, item in self.packages.items():
            src = incoming / item['filename']
            fd = os.open(src, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            with os.fdopen(fd, 'rb') as stream:
                require(stat.S_ISREG(os.fstat(stream.fileno()).st_mode), 'Package is not a regular file')
                require(os.fstat(stream.fileno()).st_size == item['size'], 'Package size mismatch')
                with (self.stage / item['filename']).open('xb') as output:
                    remaining = item['size']
                    while remaining:
                        block = stream.read(min(remaining, 1024 * 1024))
                        require(block, 'Package changed during private copy')
                        output.write(block)
                        remaining -= len(block)
                    require(not stream.read(1), 'Package grew during private copy')
            path = self.stage / item['filename']
            require(path.stat().st_size == item['size'] and digest_file(path) == item['sha256'], 'Package integrity mismatch')
            if self.platform == 'freebsd15':
                actual = command(['/usr/local/sbin/pkg', 'query', '-F', str(path), '%n\t%v\t%o'])
                require(actual == name + '\t' + item['version'] + '\tcomms/liner-notes', 'FreeBSD package identity/Origin mismatch')
                require(self.before[name][1] == 'comms/liner-notes', 'Installed FreeBSD Origin differs')
            elif self.platform == 'rocky10':
                actual = command(['/usr/bin/rpm', '-qp', '--qf', '%{NAME}\t%{VERSION}-%{RELEASE}', str(path)])
                require(actual == name + '\t' + item['version'], 'RPM metadata differs from signed manifest')
            else:
                actual = command(['/usr/bin/dpkg-deb', '-f', str(path), 'Package', 'Version'])
                require(actual == 'Package: ' + name + '\nVersion: ' + item['version'], 'DEB metadata differs from signed manifest')
        # A root-owned manifest pins archives for every currently installed BBS
        # package. Copies supplied by the deploy account cannot satisfy escrow.
        escrow = json.loads(trusted(self.profile['old_package_manifest']).read_text())
        for name in self.packages:
            old = escrow[name]
            path = trusted(old['path'])
            require(old['version'] == self.before[name][0] and digest_file(path) == old['sha256'], 'Old package escrow is missing/stale')
            require(any(path.is_relative_to(Path(r)) for r in self.profile['backup_roots']['old-packages']), 'Escrow is outside the backup inventory')
        self.check_service_inventory()
        if self.platform == 'freebsd15':
            command(['/usr/local/sbin/pkg', 'repo', str(self.stage)])
            (self.stage / 'repos').mkdir()
            (self.stage / 'repos' / 'liner_notes_release.conf').write_text(
                'liner_notes_release: { url: "file://' + str(self.stage) + '", enabled: yes, signature_type: "none" }\n')
            base = ['/usr/local/sbin/pkg', '-o', 'REPOS_DIR=' + str(self.stage / 'repos'),
                    '-o', 'PKG_CACHEDIR=' + str(self.stage / 'cache')]
            command(base + ['update', '-f', '-r', 'liner_notes_release'])
            plan = command(base + ['upgrade', '-n', '-U', '-r', 'liner_notes_release', 'liner-notes'])
            # A closed repository plus no dependency candidates still needs an
            # actual upgrade plan. Never silently accept a "New" installation.
            require('UPGRADED:' in plan and 'INSTALLED:' not in plan and 'REMOVED:' not in plan,
                    'FreeBSD dry-run did not identify an upgrade-only transaction')
            entries = re.findall(r'^\s+([A-Za-z0-9_+.-]+):\s+\S+\s+->\s+\S+', plan, flags=re.MULTILINE)
            require(entries == ['liner-notes'], 'FreeBSD plan includes an unrelated or ambiguous package change')
        elif self.platform == 'rocky10':
            files = [str(self.stage / p['filename']) for p in self.packages.values()]
            command(['/usr/bin/rpm', '-U', '--test', *files])
        else:
            files = [str(self.stage / p['filename']) for p in self.packages.values()]
            command(['/usr/bin/dpkg', '--no-act', '--install', *files])
        return {n: self.before[n][0] for n in self.packages}

    def stop(self):
        for service in reversed(self.profile['services']):
            command(service_command(self.platform, service, 'stop'))
        # Root-owned PID inventory covers the runtime and configured Door/
        # transfer workers. An active worker prevents a consistent backup.
        for pidfile in self.profile['quiescence_pidfiles']:
            path = Path(pidfile)
            if path.exists():
                pid = int(path.read_text().strip())
                try:
                    os.kill(pid, 0)
                except ProcessLookupError:
                    continue
                raise ValueError('A BBS/Door/transfer process is still alive')
        import pwd
        for name in self.profile['quiescence_users']:
            user = pwd.getpwnam(name)
            require(user.pw_uid != 0, 'Root cannot be a BBS worker account')
            pgrep = '/bin/pgrep' if self.platform == 'freebsd15' else '/usr/bin/pgrep'
            processes = command([pgrep, '-u', str(user.pw_uid)], accepted=(0, 1))
            require(not processes, 'A process still runs under a dedicated BBS/Door/transfer account')

    def start(self):
        for service in self.profile['services']:
            command(service_command(self.platform, service, 'start'))

    def backup(self, identity):
        archive = self.root / (identity + '.tar')
        roots = sorted({str(Path(p)) for group in self.profile['backup_roots'].values() for p in group})
        require(roots and all(Path(p).is_absolute() and p != '/' and Path(p).exists() for p in roots), 'Invalid backup root')
        require(not any(self.root.is_relative_to(Path(p)) for p in roots), 'Backup inventory includes the deployment archive itself')
        # GNU tar and bsdtar preserve ACLs and extended attributes. No live DB is
        # copied: services and workers have been stopped first.
        tar = '/usr/bin/tar'
        command([tar, '--acls', '--xattrs', '-cpf', str(archive), '-C', '/', *[p.lstrip('/') for p in roots]])
        checksum = digest_file(archive)
        restored = self.root / (identity + '-restore')
        restored.mkdir(mode=0o700)
        # This verification copy is disposable, never a live restore. Python's
        # data filter rejects escaping symlinks and special device nodes.
        with tarfile.open(archive) as stream:
            stream.extractall(restored, filter='data')
        checked = 0
        for path in restored.rglob('*'):
            if not path.is_file() or path.is_symlink():
                continue
            with path.open('rb') as stream:
                magic = stream.read(16)
            if magic == b'SQLite format 3\x00':
                with contextlib.closing(sqlite3.connect(path.as_uri() + '?mode=ro', uri=True)) as db:
                    require(db.execute('PRAGMA integrity_check').fetchall() == [('ok',)], 'Restored database integrity failed')
                checked += 1
        require(checked > 0, 'No SQLite database was restored')
        # The fixed verifier validates the complete restored set; no arbitrary
        # root commands or shell scripts are accepted through the request.
        from deploy_restore import verify_restored, verify_application
        verify_restored(restored, roots, self.profile['database_checks'])
        verify_application(restored, self.profile['restore_application'], trusted)
        sink = self.profile['backup_sink']
        require(re.fullmatch(r'[a-zA-Z0-9.-]+', sink['host']) and sink['host'] not in PLATFORMS,
                'Backup destination must be a separate non-production host')
        require(HEX64.fullmatch(sink['machine_id']) and sink['machine_id'] != machine_identity(),
                'Backup destination must have a different pinned OS machine identity')
        require(re.fullmatch(r'[a-z_][a-z0-9_-]*', sink['user']), 'Invalid backup user')
        key, hosts = trusted(sink['key']), trusted(sink['known_hosts'])
        backup_id = self.target + '-' + identity
        request = json.dumps({'id': backup_id, 'sha256': checksum, 'size': archive.stat().st_size}, separators=(',', ':'))
        argv = ['/usr/bin/ssh', '-T', '-i', str(key), '-o', 'BatchMode=yes', '-o', 'IdentitiesOnly=yes',
                '-o', 'StrictHostKeyChecking=yes', '-o', 'UserKnownHostsFile=' + str(hosts),
                sink['user'] + '@' + sink['host'], '/usr/local/libexec/liner-notes-backup-store ' + shlex.quote(request)]
        # Meta uses only integer/hex fields; the remote command contains no user
        # path. The sink is independently root-configured and writes privately.
        with archive.open('rb') as source:
            result = subprocess.run(argv, stdin=source, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=3600)
        require(result.returncode == 0, 'Off-host backup transfer failed')
        received = json.loads(result.stdout)
        require(received == {'id': backup_id, 'sha256': checksum, 'size': archive.stat().st_size,
                             'machine_id': sink['machine_id']}, 'Off-host backup verification failed')
        return {'id': backup_id, 'sha256': checksum, 'restored': True, 'off_host_verified': True}

    def install(self, release):
        for argv in package_commands(self.platform, self.packages, self.stage):
            command(argv)
        after = self.inventory()
        require({n:v for n,v in after.items() if n not in self.packages} ==
                {n:v for n,v in self.before.items() if n not in self.packages}, 'Unrelated package inventory changed')
        require(all(after[n][0] == p['version'] for n,p in self.packages.items()), 'Installed version mismatch')

    def health(self, release):
        from deploy_restore import health_probes
        for service in self.profile['services']:
            command(service_command(self.platform, service, 'status'))
        if self.platform == 'freebsd15':
            expected_binary = '/usr/local/sbin/liner-notes'
            pid = int(Path(self.profile['runtime_pidfile']).read_text().strip())
            require(pid > 1, 'Invalid runtime PID')
            rows = command(['/usr/bin/procstat', '-b', str(pid)]).splitlines()
            require(len(rows) == 2 and rows[1].split()[-1] == expected_binary,
                    'Runtime does not execute the packaged binary; inspect rc.conf program override')
            import pwd
            owner_uid = int(command(['/bin/ps', '-o', 'uid=', '-p', str(pid)]))
            require(owner_uid != 0 and owner_uid in {pwd.getpwnam(n).pw_uid for n in self.profile['quiescence_users']},
                    'Runtime owner is absent from quiescence inventory')
        else:
            expected_binary = '/usr/sbin/liner-notes'
            pid = int(command(['/usr/bin/systemctl', 'show', '-p', 'MainPID', '--value', 'liner-notes.service']))
            require(pid > 1 and str(Path('/proc/' + str(pid) + '/exe').readlink()) == expected_binary,
                    'Runtime does not execute the packaged binary; inspect systemd override')
        require(self.profile['restore_application']['executable'] == expected_binary,
                'Restore probe must use the same packaged executable')
        health_probes(self.profile['probes'])
        inventory = self.inventory()
        if release:
            expected = release['platforms'][self.platform]
            require(all(inventory[n][0] == expected[n]['version'] for n in inventory if n in PACKAGES), 'Health version mismatch')
        return {n:v[0] for n,v in inventory.items() if n in PACKAGES}


def main():
    host = release = None
    try:
        require(len(sys.argv) == 1 and os.geteuid() == 0, 'Dedicated root helper takes no arguments')
        os.umask(0o077)
        profile = json.loads(trusted(CONFIG).read_text())
        request = json.loads(sys.stdin.buffer.read(1024 * 1024))
        require(set(request) == {'release', 'authorization'}, 'Invalid deployment request')
        release_key = trusted(profile['release_public_key']).read_bytes()
        approval_key = trusted(profile['approval_public_key']).read_bytes()
        require(public_key_identity(release_key) != public_key_identity(approval_key), 'Release signing and deployment approval keys must be separate')
        release = validate_release(verify(request['release'], release_key))
        auth = verify(request['authorization'], approval_key)
        validate_authorization(auth, release, profile['target'])
        host = Host(profile)
        host.tested_previous_version = auth.get('freebsd_previous_version')
        result = apply_host(release, auth, profile['target'], host)
        print(json.dumps(result, sort_keys=True))
        return 0
    except Exception:
        if host is not None and release is not None:
            try:
                state = host.load_state(transaction_id(release))
            except Exception:
                state = None
            if state:
                print(json.dumps(state, sort_keys=True))
                return 1
        print('{"result":"rejected","reason":"host-deployment-validation-or-operation-failed"}')
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
