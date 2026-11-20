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


def private_solaris_dependency(target):
    """Use the explicitly prepared CI-only library without changing host paths."""
    if target != 'solaris':
        return None
    prefix = Path.home() / 'liner-notes-deps/libssh-0.12.2/local'
    metadata = prefix / 'lib/pkgconfig/libssh.pc'
    if not metadata.is_file():
        return None  # ordinary installed-library detection still must succeed
    previous = os.environ.get('PKG_CONFIG_PATH', '')
    os.environ['PKG_CONFIG_PATH'] = str(metadata.parent) + (os.pathsep + previous if previous else '')
    return prefix


def solaris_stack_runtime(command):
    """Resolve the selected compiler's 64-bit stack protector runtime."""
    compiler = shlex.split(next(value[3:] for value in command if value.startswith('CC=')))
    library = Path(subprocess.check_output(compiler + ['-m64', '-print-file-name=libssp.so'], text=True).strip())
    if not library.is_absolute() or not library.is_file():
        raise RuntimeError('Installed Solaris compiler libssp is required; stack protection is not disabled')
    # Resolve the soname symlink so its actual runtime directory is recorded.
    return '-lssp -Wl,-R,' + str(library.resolve().parent)


def build_command(target):
    pkg = shutil.which('pkg-config')
    if not pkg or subprocess.run([pkg, '--atleast-version=0.10', 'libssh']).returncode:
        raise RuntimeError('Installed libssh >= 0.10 development files are required; no OS changes made')
    cflags = subprocess.check_output([pkg, '--cflags', 'libssh'], text=True).strip()
    libs = subprocess.check_output([pkg, '--libs', 'libssh'], text=True).strip()
    if target == 'solaris':
        from solaris_ci import build_arguments
        command = build_arguments()
        libs += ' ' + solaris_stack_runtime(command)
        # The CI-private shared library is not placed in the OS loader search
        # path. Record runpaths in this disposable executable instead.
        libs += ''.join(' -Wl,-R,' + flag[2:] for flag in shlex.split(libs) if flag.startswith('-L'))
    elif target == 'freebsd':
        # The source Makefile uses portable make syntax, including FreeBSD make.
        command = ['make', 'PYTHON=' + sys.executable]
    elif target in ('rocky', 'ubuntu'):
        command = ['make', 'CC=clang', 'PYTHON=' + sys.executable, 'SQLITE_CFLAGS=', 'SQLITE_LIBS=-lsqlite3']
    else:
        raise ValueError('Unknown target')
    # A fresh git archive is used; no stale binary built without SSH can pass.
    return command + ['-j2', 'runtime', 'SSH_CFLAGS=-DLINER_SSH ' + cflags, 'SSH_LIBS=' + libs]


def install_test_dependencies(target, python):
    # Solaris's vendor Paramiko omits Ed25519. An explicitly prepared wheel set
    # can supply upstream support without changing the vendor installation.
    wheels = Path.home() / 'liner-notes-deps/ssh-client-4.0.0/wheels/requirements.txt'
    if target == 'solaris' and wheels.is_file():
        subprocess.run([python, '-m', 'pip', 'install', '--no-index', '--no-deps',
                        '--ignore-installed', '--require-hashes', '-r', str(wheels)],
                       check=True, timeout=300)
    subprocess.run([python, '-m', 'pip', 'install', '--disable-pip-version-check', '--no-cache-dir',
                    '-r', str(ROOT / 'tests/ssh-requirements.txt')], check=True, timeout=300)
    subprocess.run([python, '-c',
                    "import paramiko; assert hasattr(paramiko, 'Ed25519Key') and "
                    "'ssh-ed25519' in paramiko.Transport._preferred_keys, "
                    "'SSH test client lacks Ed25519; prepare upstream Paramiko/PyNaCl; do not relax host-key tests'"],
                   check=True, timeout=30)


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
    prefix = private_solaris_dependency(args.target)
    if prefix:
        print("Using CI-private libssh: " + str(prefix), flush=True)
    command = build_command(args.target)
    print('SSH native build: ' + shlex.join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)
    # Test-only dependencies are isolated, never pip-installed into the host Python.
    venv = ROOT / '.native-ssh-venv'
    if venv.exists():
        raise ValueError('Refuse to reuse an old SSH test environment')
    creation = [sys.executable, '-m', 'venv']
    if args.target in ('freebsd', 'solaris'):
        # Reuse explicitly prepared OS cryptography/Paramiko builds; a fresh
        # venv still contains any pip additions, never the system interpreter.
        creation.append('--system-site-packages')
    subprocess.run(creation + [str(venv)], check=True)
    python = str(venv / 'bin/python')
    install_test_dependencies(args.target, python)
    subprocess.run([python, '-B', 'tests/test_ssh_transport.py', './runtime'], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
