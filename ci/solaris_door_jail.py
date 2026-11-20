#!/usr/bin/python3.13 -I
"""Separately approved Solaris nosuid/chroot capability experiment, not a backend.

Default: read-only plan. Never run automatically as root in CI. --run-approved
needs explicit approval for one disposable 64MiB tmpfs mount and fixture files.
Does not install services/accounts, edit vfstab, or enable authenticated Doors.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import stat
import signal
import subprocess
import sys
import tempfile

INSTALL = Path('/opt/liner-notes-ci/door-jail')
SOURCES = {'solaris_door_jail.py': 'ci/solaris_door_jail.py',
           'solaris_door_boundary.py': 'ci/solaris_door_boundary.py',
           'solaris_door_jail_probe.c': 'tests/solaris_door_jail_probe.c',
           'solaris_door_identity_probe.c': 'tests/solaris_door_identity_probe.c'}
ROLES = {'bbs': 61002, 'broker': 61003, 'consumer': 61004, 'other_slot': 61005, 'root': 0}
SLOT = 61001
ID_FIELDS = {'ruid', 'euid', 'suid', 'rgid', 'egid', 'sgid'}
FLAGS = {'basic_limit', 'namespace_closed', 'descriptors_closed', 'chroot_denied'}
CASES = {'plain'} | {f'{mode}-{role}' for mode in ('uid', 'gid') for role in ROLES}


def plan():
    return {'install': str(INSTALL), 'default_read_only': True,
            'requires_separate_mount_approval': True,
            'temporary_parent': '/var/tmp/liner-door-jail-*',
            'mount': {'type': 'tmpfs', 'source': 'swap', 'options': 'nosuid,size=64m'},
            'identities': [SLOT, *[v for v in ROLES.values() if v]],
            'changes_services_accounts_vfstab': False, 'door_backend_enabled': False,
            'scope': 'credentials and filesystem namespace only; lifecycle/PTY/Broker still pending'}


def sha_valid(value):
    return isinstance(value, str) and re.fullmatch(r'[a-f0-9]{40}', value) is not None


def prepare(output, sha):
    if not sha_valid(sha): raise ValueError('Full reviewed commit required')
    output = Path(output); output.mkdir(mode=0o700)
    source = Path(__file__).resolve().parents[1]
    hashes = {}
    for name, relative in SOURCES.items():
        data = (source/relative).read_bytes()
        (output/name).write_bytes(data)
        hashes[name] = hashlib.sha256(data).hexdigest()
    (output/'manifest.json').write_text(json.dumps({'source_sha': sha, 'sha256': hashes}, indent=2)+'\n', encoding='utf-8')
    return output


def trusted(path):
    # Before loading any sibling Python, including the reused root fixture.
    for item in (path, *path.parents):
        info = item.lstat()
        if stat.S_ISLNK(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError('Untrusted installed fixture path')
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_mode & 0o077:
        raise ValueError('Expected private single-link fixture file')


def verify_bundle():
    if Path(__file__).resolve() != INSTALL/'solaris_door_jail.py':
        raise ValueError('Only the fixed root-owned installation may execute')
    for name in (*SOURCES, 'manifest.json'): trusted(INSTALL/name)
    manifest = json.loads((INSTALL/'manifest.json').read_text())
    if (set(manifest) != {'source_sha', 'sha256'} or not sha_valid(manifest['source_sha'])
            or set(manifest['sha256']) != set(SOURCES)):
        raise ValueError('Invalid manifest')
    for name in SOURCES:
        if hashlib.sha256((INSTALL/name).read_bytes()).hexdigest() != manifest['sha256'][name]:
            raise ValueError('Bundle digest mismatch')
    return manifest


def library_paths(output):
    paths = {PurePosixPath('/lib/amd64/ld.so.1')}
    for line in output.splitlines():
        if not line.strip(): continue
        found = re.fullmatch(r'\s*[^\s]+\s+=>\s+(/[^\s]+)\s*', line)
        if not found: raise ValueError('Unexpected or unresolved ldd output')
        path = PurePosixPath(found[1])
        if ('..' in path.parts or not re.fullmatch(r'/(?:usr/)?lib/(?:amd64|64)/[A-Za-z0-9_.-]+', str(path))):
            raise ValueError('Dependency outside fixed Solaris 64-bit library directories')
        paths.add(path)
    if len(paths) < 2 or len(paths) > 32: raise ValueError('Incomplete or excessive dependency inventory')
    return sorted(paths)


def mount_entry(text, root):
    found = []
    for line in text.splitlines():
        fields = line.split()
        if len(fields) >= 4 and fields[1] == str(root): found.append(fields)
        elif len(fields) >= 2 and fields[1].startswith(str(root)+'/'):
            raise ValueError('Unexpected nested mount; preserve for inspection')
    if len(found) > 1: raise ValueError('Multiple mounts at private root')
    if not found: return None
    fields = found[0]
    if fields[0] != 'swap' or fields[2] != 'tmpfs':
        raise ValueError('Unexpected mount owner/type; do not unmount')
    return set(fields[3].split(','))


def require_nosuid(options):
    if options is None or not ('nosuid' in options or {'nosetuid', 'nodevices'} <= options):
        raise ValueError('nosuid not confirmed in live mount table')
    if {'suid', 'setuid', 'devices', 'noexec'} & options:
        raise ValueError('Conflicting mount options')


def evaluate(matrix):
    if set(matrix) != CASES: raise ValueError('Incomplete matrix')
    failures = []
    for name, pair in matrix.items():
        if set(pair) != {'baseline', 'jailed'}: raise ValueError('Incomplete control pair')
        before, after = pair['baseline'], pair['jailed']
        if set(before) != ID_FIELDS | {'groups', 'basic_limit'}: raise ValueError('Invalid baseline report')
        if any(type(before[k]) is not int for k in ID_FIELDS | {'groups'}) or type(before['basic_limit']) is not bool:
            raise ValueError('Invalid baseline types')
        expected = {k: SLOT for k in ID_FIELDS}
        if name != 'plain':
            mode, role = name.split('-')
            for key in (('euid', 'suid') if mode == 'uid' else ('egid', 'sgid')): expected[key] = ROLES[role]
        if any(before[k] != v for k, v in expected.items()) or before['groups'] != 0:
            raise ValueError('Baseline set-ID positive control ineffective')
        if after == {'exec_denied': True} and type(after.get('exec_denied')) is bool:
            if name == 'plain': raise ValueError('Plain executable must run inside the root')
            continue
        if set(after) != ID_FIELDS | {'groups'} | FLAGS: raise ValueError('Incomplete jailed report')
        if any(type(after[k]) is not int for k in ID_FIELDS | {'groups'}) or any(type(after[k]) is not bool for k in FLAGS):
            raise ValueError('Invalid jailed report types')
        if any(after[k] != SLOT for k in ID_FIELDS) or after['groups'] != 0 or not all(after[k] for k in FLAGS):
            failures.append(name)
    return {'credential_namespace_confined': not failures, 'failed_cases': failures,
            'broker_isolation_verified': False, 'door_backend_enabled': False,
            'lifecycle_verified': False}


def read_mount(root):
    return mount_entry(Path('/etc/mnttab').read_text(), root)


def command(args, report, timeout=15):
    args = [str(a) for a in args]
    entry = {'argv': args}
    report.setdefault('commands', []).append(entry)
    process = subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True, close_fds=True,
                               start_new_session=True, env={'PATH':'/usr/bin:/usr/sbin','LC_ALL':'C'})
    try:
        output, error = process.communicate(timeout=timeout)
    except BaseException as failure:
        try: os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError: pass
        output, error = process.communicate()
        entry.update(timeout=isinstance(failure, subprocess.TimeoutExpired),
                     interrupted=type(failure).__name__, exit_code=process.returncode,
                     stdout=output[-65536:], stderr=error[-65536:])
        raise
    entry.update(exit_code=process.returncode, stdout=output[-65536:], stderr=error[-65536:])
    if process.returncode:
        raise subprocess.CalledProcessError(process.returncode, args)
    return subprocess.CompletedProcess(args, process.returncode, output, error)


def run_locked(base, manifest):
    compiler = base.compiler_path()
    # Fixed trusted OS utilities only; never PATH or a caller-selected command.
    utilities = {}
    for name, location in {'mount':'/usr/sbin/mount', 'umount':'/usr/sbin/umount', 'ldd':'/usr/bin/ldd'}.items():
        resolved = Path(location).resolve(strict=True); base.trusted(resolved, private=False)
        utilities[name] = resolved
    work = Path(tempfile.mkdtemp(prefix='liner-door-jail-', dir='/var/tmp'))
    root = work/'root'; root.mkdir(mode=0o700)
    outside, inside, directories = [], [], []
    mount_requested = False
    report = {'source_sha': manifest['source_sha'], 'bundle_sha256': manifest['sha256'],
              'system': platform.platform(), 'directory': str(work), 'cases': {},
              'broker_isolation_verified': False, 'door_backend_enabled': False,
              'lifecycle_verified': False, 'cleanup_complete': False}
    # Durable breadcrumb before compiling/mounting, even if the supervisor is
    # killed and cannot reach finally. It contains only synthetic test metadata.
    with (work/'plan.json').open('x',encoding='utf-8') as stream:
        os.chmod(work/'plan.json',0o600)
        json.dump(dict(report, planned_mount=str(root), mount_options='nosuid,size=64m'),stream,indent=2)
        stream.flush(); os.fsync(stream.fileno())
    def invoke(args, timeout=15): return command(args, report, timeout)
    try:
        binary = work/'observer'; baseline = work/'baseline'
        for source, destination in [('solaris_door_jail_probe.c',binary), ('solaris_door_identity_probe.c',baseline)]:
            outside.append(destination)
            invoke([compiler,'-m64','-std=c11','-Wall','-Wextra','-Werror',INSTALL/source,'-o',destination], timeout=45)
            os.chmod(destination,0o555)
        base.identity_available(); invoke([baseline,'--inventory'])
        libraries = library_paths(invoke([utilities['ldd'], binary]).stdout)
        if read_mount(root) is not None: raise ValueError('New directory is already mounted')
        mount_requested = True
        invoke([utilities['mount'],'-F','tmpfs','-o','nosuid,size=64m','swap',root])
        options = read_mount(root); require_nosuid(options)
        report['mount_options'] = sorted(options)
        os.chmod(root,0o755)
        total = 0; report['libraries'] = []
        for library in libraries:
            origin = Path(str(library)).resolve(strict=True); base.trusted(origin, private=False)
            size = origin.stat().st_size; total += size
            if size <= 0 or total > 32*1024*1024: raise ValueError('Library copy budget exceeded')
            destination = root/str(library).lstrip('/')
            for directory in reversed(destination.parent.parents):
                if directory != root and root in directory.parents and not directory.exists():
                    directory.mkdir(mode=0o755); directories.append(directory)
            if not destination.parent.exists():
                destination.parent.mkdir(mode=0o755); directories.append(destination.parent)
            inside.append(destination); shutil.copyfile(origin,destination); os.chmod(destination,0o555)
            report['libraries'].append({'path':str(library),'sha256':hashlib.sha256(destination.read_bytes()).hexdigest()})
        # Baseline controls need traversal after dropping identity; root retains
        # all write permissions. The unregistered numeric IDs are never reused.
        os.chmod(work,0o711)
        for name in sorted(CASES):
            uid, gid, mode = SLOT, SLOT, 0o555
            if name != 'plain':
                kind, role = name.split('-')
                if kind == 'uid': uid, mode = ROLES[role], 0o4555
                else: gid, mode = ROLES[role], 0o2555
            control, jailed = work/('control-'+name), root/name
            outside.append(control); inside.append(jailed)
            for source, target in ((baseline,control),(binary,jailed)):
                shutil.copyfile(source,target); os.chown(target,uid,gid); os.chmod(target,mode)
            report['cases'][name] = {'baseline':base.parse_report(invoke([baseline,'--baseline',control]).stdout),
                                     'jailed':json.loads(invoke([binary,'--observe',root,name]).stdout)}
        report.update(evaluate(report['cases']))
    except BaseException as error:
        report['error'] = type(error).__name__
        if hasattr(error,'returncode'): report['exit_code'] = error.returncode
        raise
    finally:
        os.chmod(work,0o700)
        # No recursive removal, forced unmount, broad PID kill, or deletion on
        # uncertain mount identity. Failure retains an exact root-only receipt.
        try:
            mounted = read_mount(root) if mount_requested else None
            if mounted is not None:
                base.remove_fixture_files([p for p in inside if os.path.lexists(p)])
                for directory in reversed(directories): directory.rmdir()
                invoke([utilities['umount'],root])
                if read_mount(root) is not None: raise ValueError('Mount remains after umount')
            elif inside:
                raise ValueError('Mount vanished unexpectedly; preserve host paths')
            base.remove_fixture_files([p for p in outside if os.path.lexists(p)])
            root.rmdir()
            report['cleanup_complete'] = True
        except Exception as error:
            report['cleanup_error'] = type(error).__name__
            raise
        finally:
            receipt = work/'result.json'
            with receipt.open('x',encoding='utf-8') as stream:
                os.chmod(receipt,0o600); json.dump(report,stream,indent=2)
            print(json.dumps({'receipt':str(receipt),'source_sha':manifest['source_sha'],
                              'cleanup_complete':report['cleanup_complete']}))
    return report


def run():
    if platform.system() != 'SunOS' or os.getuid() != 0 or os.geteuid() != 0:
        raise ValueError('Requires Solaris real/effective root and separate mount approval')
    manifest = verify_bundle()
    specification = importlib.util.spec_from_file_location('approved_boundary',INSTALL/'solaris_door_boundary.py')
    base = importlib.util.module_from_spec(specification)
    previous_bytecode = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    try: specification.loader.exec_module(base)
    finally: sys.dont_write_bytecode = previous_bytecode
    import fcntl
    # The prior credential fixture uses the same synthetic identities. Share its
    # already installed lock rather than racing an independently locked run.
    shared_lock = base.INSTALL/'fixture.lock'
    base.trusted(shared_lock)
    lock = os.open(str(shared_lock),os.O_RDWR | os.O_NOFOLLOW)
    try:
        info = os.fstat(lock)
        if not stat.S_ISREG(info.st_mode) or info.st_uid or info.st_nlink != 1 or info.st_mode & 0o077:
            raise ValueError('Untrusted private lock')
        fcntl.flock(lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
        report = run_locked(base,manifest)
    finally:
        os.close(lock)
    return 0 if report['credential_namespace_confined'] else 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--plan',action='store_true')
    action.add_argument('--prepare',metavar='NEW_DIRECTORY')
    action.add_argument('--run-approved',action='store_true')
    parser.add_argument('--source-sha')
    args = parser.parse_args()
    if args.prepare: print(prepare(args.prepare,args.source_sha))
    elif args.run_approved:
        def interrupted(number, frame): raise InterruptedError('Fixture interrupted')
        previous = signal.signal(signal.SIGTERM,interrupted)
        try: return run()
        finally: signal.signal(signal.SIGTERM,previous)
    else: print(json.dumps(plan(),indent=2))
    return 0


if __name__ == '__main__': raise SystemExit(main())
