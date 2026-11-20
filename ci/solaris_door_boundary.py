#!/usr/bin/python3.13 -I
"""Approval-gated Solaris set-ID capability fixture; never enables Door launch.

No service, account, mount, sudo or package changes. Default is a read-only plan.
The root run accepts only an installed, root-owned immutable bundle and operates
inside one newly created /var/tmp directory. No caller-selected UID or command.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import signal
import stat
import subprocess
import sys
import tempfile

INSTALL = Path('/opt/liner-notes-ci/door-boundary')
FILES = ('solaris_door_boundary.py', 'solaris_door_identity_probe.c')
IDS = {'slot': 61001, 'bbs': 61002, 'broker': 61003, 'consumer': 61004, 'other_slot': 61005}
ENV = {'PATH': '/usr/bin:/usr/sbin', 'LC_ALL': 'C'}
FIELDS = {'ruid', 'euid', 'suid', 'rgid', 'egid', 'sgid', 'groups', 'basic_limit'}


def plan():
    return {'purpose': 'credential capability observation; not Broker acceptance',
            'install': str(INSTALL), 'uids_and_gids': IDS,
            'temporary_parent': '/var/tmp', 'mutations': ['private fixture files only'],
            'creates_accounts': False, 'changes_services': False,
            'changes_mounts': False, 'enables_solaris_door': False}


def prepare(output, source_sha):
    if len(source_sha) != 40 or any(c not in '0123456789abcdef' for c in source_sha):
        raise ValueError('Full reviewed source commit is required')
    root = Path(__file__).resolve().parents[1]
    sources = {'solaris_door_boundary.py': root/'ci/solaris_door_boundary.py',
               'solaris_door_identity_probe.c': root/'tests/solaris_door_identity_probe.c'}
    output = Path(output)
    output.mkdir(mode=0o700)  # no replacement of an approved bundle
    hashes = {}
    for name, source in sources.items():
        data = source.read_bytes()
        (output/name).write_bytes(data)
        hashes[name] = hashlib.sha256(data).hexdigest()
    (output/'manifest.json').write_text(json.dumps({'source_sha': source_sha, 'sha256': hashes}, indent=2)+'\n')
    return output


def trusted(path, directory=False, private=True):
    path = Path(path)
    for item in (path, *path.parents):
        info = item.lstat()
        if stat.S_ISLNK(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError('Untrusted root fixture path')
    info = path.lstat()
    if directory:
        if not stat.S_ISDIR(info.st_mode): raise ValueError('Expected directory')
    elif not stat.S_ISREG(info.st_mode) or (private and info.st_nlink != 1):
        raise ValueError('Expected private regular fixture file')


def compiler_path():
    compiler = Path('/usr/bin/gcc').resolve(strict=True)
    # Solaris's packaged GCC has multiple root-owned aliases of the same inode.
    # It is a protected system executable, not a disposable private fixture.
    # Ownership, ancestor trust, no symlinks and no group/other writes still apply.
    trusted(compiler, private=False)
    return compiler


def verify_bundle():
    if Path(__file__).resolve() != INSTALL/'solaris_door_boundary.py':
        raise ValueError('Run only the fixed installed fixture')
    for name in (*FILES, 'manifest.json'): trusted(INSTALL/name)
    manifest = json.loads((INSTALL/'manifest.json').read_text())
    if set(manifest) != {'source_sha', 'sha256'} or set(manifest['sha256']) != set(FILES):
        raise ValueError('Invalid bundle manifest')
    sha = manifest['source_sha']
    if not isinstance(sha, str) or len(sha) != 40 or any(c not in '0123456789abcdef' for c in sha):
        raise ValueError('Invalid source commit')
    for name in FILES:
        if hashlib.sha256((INSTALL/name).read_bytes()).hexdigest() != manifest['sha256'][name]:
            raise ValueError('Bundle checksum mismatch')
    return sha


def identity_available():
    import pwd, grp
    for value in IDS.values():
        for lookup in (pwd.getpwuid, grp.getgrgid):
            try: lookup(value)
            except KeyError: continue
            raise ValueError('A reserved test identity already exists; do not reuse it')


def command(args, timeout=15):
    process = subprocess.Popen([str(a) for a in args], stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                               env=ENV, close_fds=True, start_new_session=True)
    try:
        output, error = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        try: os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError: pass
        process.communicate()
        raise
    if process.returncode:
        raise subprocess.CalledProcessError(process.returncode, args)
    return subprocess.CompletedProcess(args, process.returncode, output, error)


def parse_report(output, allow_denied=False):
    value = json.loads(output)
    if (allow_denied and isinstance(value, dict) and set(value) == {'exec_denied', 'errno'}
            and value['exec_denied'] is True and type(value['errno']) is int):
        import errno
        if value['errno'] in (errno.EPERM, errno.EACCES): return value
    if not isinstance(value, dict) or set(value) != FIELDS:
        raise ValueError('Incomplete credential report')
    if type(value['basic_limit']) is not bool:
        raise ValueError('Invalid privilege report')
    if any(type(value[key]) is not int or value[key] < 0 for key in FIELDS-{'basic_limit'}):
        raise ValueError('Invalid credential values')
    return value


def confined(value):
    return (all(value[key] == IDS['slot'] for key in FIELDS-{'groups', 'basic_limit'})
            and value['groups'] == 0 and value['basic_limit'])


def evaluate(cases):
    # Controls establish executable/set-ID behavior. An ignored set-ID mount or
    # failed exec is not evidence that a per-process privilege barrier worked.
    expected = {'plain'} | {f'{mode}-{role}' for mode in ('uid', 'gid')
                           for role in ('bbs', 'broker', 'consumer', 'other_slot', 'root')}
    if set(cases) != expected: raise ValueError('Incomplete case matrix')
    escapes = []
    for name, case in cases.items():
        before = parse_report(json.dumps(case['baseline']))
        after = parse_report(json.dumps(case['basic']), allow_denied=name != 'plain')
        for value in (before, *([] if after.get('exec_denied') else [after])):
            if value['ruid'] != IDS['slot'] or value['rgid'] != IDS['slot'] or value['groups'] != 0:
                raise ValueError('Drop-to-slot precondition not preserved')
        if name == 'plain':
            if any(before[key] != IDS['slot'] for key in FIELDS-{'groups', 'basic_limit'}):
                raise ValueError('Plain control changed identity')
            if not confined(after): raise ValueError('Plain execution did not retain basic slot state')
            continue
        mode, role = name.split('-')
        target = 0 if role == 'root' else IDS[role]
        fields = ('euid', 'suid') if mode == 'uid' else ('egid', 'sgid')
        if any(before[key] != target for key in fields):
            raise ValueError('Set-ID control ineffective; observation is inconclusive')
        if any(before[key] != IDS['slot'] for key in FIELDS-{'groups', 'basic_limit'}-set(fields)):
            raise ValueError('Control changed an unrelated identity')
        if not after.get('exec_denied') and not confined(after): escapes.append(name)
    return {'setid_confined': not escapes, 'identity_escapes': escapes,
            'broker_isolation_verified': False, 'door_backend_enabled': False}


def remove_fixture_files(paths):
    # Only known synthetic files, never recursive removal of a computed path.
    for path in reversed(paths):
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError('Unexpected fixture object; preserve for inspection')
        os.chmod(path, 0o600)
        path.unlink()


def run():
    if platform.system() != 'SunOS' or os.getuid() != 0 or os.geteuid() != 0:
        raise ValueError('Solaris real/effective root required; no automatic elevation')
    source_sha = verify_bundle()
    identity_available()
    # Serialise this fixture only. No use of another user's native CI lock.
    import fcntl
    lock = os.open(str(INSTALL/'fixture.lock'), os.O_RDWR | os.O_NOFOLLOW)
    try:
        info = os.fstat(lock)
        if not stat.S_ISREG(info.st_mode) or info.st_uid or info.st_nlink != 1 or info.st_mode & 0o077:
            raise ValueError('Untrusted fixture lock')
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        return run_locked(source_sha)
    finally:
        os.close(lock)


def run_locked(source_sha):
    compiler = compiler_path()
    root = Path(tempfile.mkdtemp(prefix='liner-door-boundary-', dir='/var/tmp'))
    paths = []
    report = {'source_sha': source_sha, 'system': platform.platform(), 'directory': str(root),
              'cases': {}, 'broker_isolation_verified': False}
    try:
        binary = root/'probe'
        paths.append(binary)
        command([compiler, '-m64', '-std=c11', '-Wall', '-Wextra', '-Werror',
                 INSTALL/'solaris_door_identity_probe.c', '-o', binary], timeout=45)
        os.chmod(binary, 0o755)
        command([binary, '--inventory'])  # saved/real/effective IDs, read only
        # Ancestors cannot be renamed by a slot; it can traverse, never list or
        # write this directory. Numeric test identities are not system accounts.
        os.chmod(root, 0o711)
        matrix = [('plain', IDS['slot'], IDS['slot'], 0o555)]
        for role in ('bbs', 'broker', 'consumer', 'other_slot', 'root'):
            target = 0 if role == 'root' else IDS[role]
            matrix.extend([(f'uid-{role}', target, IDS['slot'], 0o4555),
                           (f'gid-{role}', IDS['slot'], target, 0o2555)])
        for name, uid, gid, mode in matrix:
            path = root/name
            paths.append(path)
            shutil.copyfile(binary, path)
            os.chown(path, uid, gid)
            os.chmod(path, mode)
            report['cases'][name] = {kind: parse_report(command([binary, '--'+kind, path]).stdout,
                                                       allow_denied=kind == 'basic' and name != 'plain')
                                     for kind in ('baseline', 'basic')}
        report.update(evaluate(report['cases']))
    except Exception as error:
        report['error'] = type(error).__name__
        if isinstance(error, subprocess.CalledProcessError): report['exit_code'] = error.returncode
        raise
    finally:
        # Always remove privilege bits/files; retain only a root-readable JSON
        # receipt. A failed cleanup leaves the directory private and raises.
        os.chmod(root, 0o700)
        remove_fixture_files([p for p in paths if os.path.lexists(p)])
        command_cleanup = not any(os.path.lexists(p) for p in paths)
        report['synthetic_files_removed'] = command_cleanup
        receipt = root/'result.json'
        with receipt.open('x', encoding='utf-8') as stream:
            os.chmod(receipt, 0o600)
            json.dump(report, stream, indent=2)
        print(json.dumps({'receipt': str(receipt), 'source_sha': source_sha,
                          'synthetic_files_removed': command_cleanup}))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--plan', action='store_true')
    selection.add_argument('--run', action='store_true')
    selection.add_argument('--prepare', metavar='NEW_DIRECTORY')
    parser.add_argument('--source-sha')
    args = parser.parse_args()
    if args.prepare:
        print(prepare(args.prepare, args.source_sha or ''))
    elif args.run:
        report = run()
        return 0 if report['setid_confined'] else 2
    else:
        print(json.dumps(plan(), indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
