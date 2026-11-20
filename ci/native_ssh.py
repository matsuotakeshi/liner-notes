#!/usr/bin/env python3
"""Encrypted SSH-only native suite; uses installed libraries, never changes OS packages."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def build_command(target):
    pkg = shutil.which('pkg-config')
    if not pkg or subprocess.run([pkg, '--atleast-version=0.10', 'libssh']).returncode:
        raise RuntimeError('Installed libssh >= 0.10 development files are required; no OS changes made')
    cflags = subprocess.check_output([pkg, '--cflags', 'libssh'], text=True).strip()
    libs = subprocess.check_output([pkg, '--libs', 'libssh'], text=True).strip()
    if target == 'solaris':
        from solaris_ci import build_arguments
        command = build_arguments()
    elif target == 'freebsd':
        # The source Makefile uses portable make syntax, including FreeBSD make.
        command = ['make', 'PYTHON=' + sys.executable]
    elif target in ('rocky', 'ubuntu'):
        command = ['make', 'CC=clang', 'PYTHON=' + sys.executable, 'SQLITE_CFLAGS=', 'SQLITE_LIBS=-lsqlite3']
    else:
        raise ValueError('Unknown target')
    # A fresh git archive is used; no stale binary built without SSH can pass.
    return command + ['-j2', 'runtime', 'SSH_CFLAGS=-DLINER_SSH ' + cflags, 'SSH_LIBS=' + libs]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=('freebsd', 'rocky', 'ubuntu', 'solaris'), required=True)
    args = parser.parse_args()
    from native_remote import verify_os
    verify_os(args.target)
    if os.geteuid() == 0:
        raise ValueError('Native SSH tests must run as the existing non-root CI account')
    for tool in ('ssh', 'ssh-keygen'):
        if not shutil.which(tool):
            raise RuntimeError('Required installed client missing: ' + tool)
    command = build_command(args.target)
    print('SSH native build: ' + shlex.join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)
    # Test-only dependencies are isolated, never pip-installed into the host Python.
    venv = ROOT / '.native-ssh-venv'
    if venv.exists():
        raise ValueError('Refuse to reuse an old SSH test environment')
    subprocess.run([sys.executable, '-m', 'venv', str(venv)], check=True)
    python = str(venv / 'bin/python')
    subprocess.run([python, '-m', 'pip', 'install', '--disable-pip-version-check', '--no-cache-dir',
                    '-r', str(ROOT / 'tests/ssh-requirements.txt')], check=True, timeout=300)
    subprocess.run([python, '-B', 'tests/test_ssh_transport.py', './runtime'], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
