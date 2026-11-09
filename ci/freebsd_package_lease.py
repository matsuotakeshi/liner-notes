#!/usr/bin/env python3
"""Existing CI-account package build with the native host lock and heartbeat."""
import argparse
import base64
import json
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import sys
import threading
import time

from native_remote import host_lock, run_commands, verify_os

ROOT = Path(__file__).resolve().parents[1]


def locked_commands(path, commands, heartbeat_fd, timeout=3300, lease=45, stopped=lambda: False):
    deadline = time.monotonic() + timeout
    with host_lock(path, heartbeat_fd, min(1200, timeout), lease, stopped) as state:
        run_commands(commands, heartbeat_fd, max(0, deadline-time.monotonic()), lease, stopped, state)


def validate_work(directory, sha, digest):
    path = Path(directory)
    if (not re.fullmatch(r'freebsd-pkg-[A-Za-z0-9]+', path.name) or
            not path.is_absolute() or path.parent.resolve() != (Path.home()/'ci').resolve() or
            not re.fullmatch(r'[0-9a-f]{40}', sha) or not re.fullmatch(r'[0-9a-f]{64}', digest)):
        raise ValueError('Invalid existing CI workspace/source identity')
    return path


def remote(directory, sha, digest):
    verify_os('freebsd')
    if os.geteuid() == 0:
        raise ValueError('Package worker must use the existing unprivileged CI account')
    path = validate_work(directory, sha, digest)
    if (path/'supervisor/source.sha').read_text(encoding='ascii').strip() != sha:
        raise ValueError('Package supervisor source identity differs')
    stopped = threading.Event()
    for number in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(number, lambda number, frame: stopped.set())
    locked_commands(Path.home()/'ci/.liner-notes-native.lock',
                    [['/bin/sh', str(path/'supervisor/build-freebsd-package.sh'), str(path), sha, digest]],
                    sys.stdin.fileno(), stopped=stopped.is_set)


def runner(directory, sha, digest):
    from native_ci import require_trusted_main, ssh_options, run_setup, supervise, TARGETS
    head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    require_trusted_main(os.environ, sha, head)
    names = ['ci/freebsd_package_lease.py', 'ci/native_ci.py', 'ci/native_remote.py', 'ci/build-freebsd-package.sh']
    if subprocess.check_output(['git', 'diff', 'HEAD', '--', *names], cwd=ROOT):
        raise ValueError('Package supervisor differs from the committed main source')
    if not re.fullmatch(r'/[A-Za-z0-9_./-]+/ci/freebsd-pkg-[A-Za-z0-9]+', directory) or '..' in directory.split('/'):
        raise ValueError('Unexpected remote workspace')
    if not re.fullmatch(r'[0-9a-f]{64}', digest):
        raise ValueError('Invalid archive digest')
    files = {Path(name).name: base64.b64encode((ROOT/name).read_bytes()).decode('ascii')
             for name in names if name != 'ci/native_ci.py'}
    payload = base64.b64encode(json.dumps(files).encode()).decode('ascii')
    loader = ('import base64,json,sys;from pathlib import Path;'
              'p=Path(sys.argv[1])/"supervisor";p.mkdir(mode=0o700);'
              'd=json.loads(base64.b64decode(sys.argv[3]));'
              '[(p/n).write_bytes(base64.b64decode(v)) for n,v in d.items()];'
              '(p/"source.sha").write_text(sys.argv[2]+"\\n",encoding="ascii")')
    host, key = TARGETS['freebsd']
    ssh = ['ssh', *ssh_options(key), host]
    stopped = threading.Event()
    for number in (signal.SIGTERM, signal.SIGINT):
        signal.signal(number, lambda number, frame: stopped.set())
    run_setup(ssh + [shlex.join(['python3', '-c', loader, directory, sha, payload])], sys.stdout.buffer, stopped)
    code = supervise(ssh + [shlex.join(['python3', str(Path(directory)/'supervisor/freebsd_package_lease.py'),
                                      'remote', directory, sha, digest])], sys.stdout.buffer, stopped, timeout=3400)
    if code or stopped.is_set():
        raise RuntimeError('Package lease worker failed/cancelled: ' + str(code))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['runner', 'remote'])
    parser.add_argument('directory')
    parser.add_argument('sha')
    parser.add_argument('digest')
    args = parser.parse_args()
    (runner if args.mode == 'runner' else remote)(args.directory, args.sha, args.digest)


if __name__ == '__main__':
    main()
