#!/usr/bin/python3 -I
"""Root-owned, main-only entrypoint for the designated Rocky SELinux CI VM.

Install this reviewed file separately; never grant sudo to a checkout path.
The only argument is a commit SHA. Source is obtained independently from the
fixed public repository, never from the SSH user's filesystem or environment.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
import urllib.request

REPOSITORY = 'matsuotakeshi/liner-notes'
BASE = Path('/var/lib/liner-notes-ci/selinux')
ENV = {'PATH': '/usr/sbin:/usr/bin:/sbin:/bin:/usr/local/bin', 'HOME': '/root',
       'LANG': 'C.UTF-8', 'PYTHONUTF8': '1', 'PYTHONNOUSERSITE': '1'}


def command(*args, cwd=None, timeout=300):
    return subprocess.run([str(a) for a in args], cwd=cwd, env=ENV,
                          check=True, text=True, capture_output=True, timeout=timeout)


def get(url, limit):
    request = urllib.request.Request(url, headers={'User-Agent': 'liner-notes-ci',
                                                  'Accept': 'application/vnd.github+json'})
    with urllib.request.urlopen(request, timeout=60) as response:
        content = response.read(limit + 1)
    if len(content) > limit:
        raise ValueError('Source response exceeds its fixed size budget')
    return content


def validate_sha(sha):
    if not re.fullmatch(r'[0-9a-f]{40}', sha):
        raise ValueError('Expected an exact lowercase commit SHA')
    return sha


def validate_account(account, caller, fixture_groups, ssh_groups):
    if (account.pw_uid in (0, caller.pw_uid) or account.pw_gid == 0 or
            Path(account.pw_shell).name != 'nologin' or
            0 in fixture_groups or account.pw_gid in ssh_groups):
        raise ValueError('Fixture account must be non-login, non-root and separate from the SSH user')


def current_main():
    return validate_sha(json.loads(get('https://api.github.com/repos/' + REPOSITORY +
                                     '/git/ref/heads/main', 65536))['object']['sha'])


def extract(content, destination, sha):
    """No links/devices, duplicate paths, writable modes, or escaping members."""
    prefix = 'liner-notes-' + validate_sha(sha)
    seen = set()
    total = 0
    with tarfile.open(fileobj=io.BytesIO(content), mode='r:gz') as archive:
        members = archive.getmembers()
        if len(members) > 20000:
            raise ValueError('Too many source archive members')
        for member in members:
            path = PurePosixPath(member.name)
            if (path.is_absolute() or not path.parts or path.parts[0] != prefix or
                    '..' in path.parts or '\\' in member.name or
                    member.name in seen or not (member.isfile() or member.isdir())):
                raise ValueError('Unsafe source archive member')
            seen.add(member.name)
            total += member.size
            if total > 512 * 1024 * 1024:
                raise ValueError('Source extraction exceeds its size budget')
        for member in members:
            parts = PurePosixPath(member.name).parts[1:]
            target = destination.joinpath(*parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True, mode=0o755)
            else:
                target.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
                with target.open('xb') as output, archive.extractfile(member) as source:
                    shutil.copyfileobj(source, output)
                target.chmod(0o755 if member.mode & 0o111 else 0o644)


def safe_base():
    for path in reversed([BASE, *BASE.parents]):
        if path.exists() or path.is_symlink():
            info = path.lstat()
            if not stat.S_ISDIR(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
                raise ValueError('CI directory ancestry must be root-owned and not writable by others')
        else:
            path.mkdir(mode=0o700)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sha')
    args = parser.parse_args()
    sha = validate_sha(args.sha)
    os.environ.clear()
    os.environ.update(ENV)
    os.umask(0o077)
    if os.geteuid() != 0:
        raise ValueError('Installed root-owned helper requires its restricted sudo rule')
    if '10.0.0.51' not in command('/usr/bin/hostname', '-I').stdout.split():
        raise ValueError('Refuse any host except the designated Rocky test VM')
    if 'ID="rocky"' not in Path('/etc/os-release').read_text():
        raise ValueError('Expected Rocky Linux')
    if command('/usr/sbin/getenforce').stdout.strip() != 'Enforcing':
        raise ValueError('Enforcing required; this helper never changes SELinux mode')
    if sha != current_main():
        raise ValueError('Only the current reviewed main commit is accepted')
    import pwd
    account = pwd.getpwnam('liner_notes_ci')
    caller = pwd.getpwnam('ci-test')
    validate_account(account, caller, os.getgrouplist(account.pw_name, account.pw_gid),
                     os.getgrouplist(caller.pw_name, caller.pw_gid))
    safe_base()
    import fcntl
    with (BASE / 'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        existing = command('/usr/sbin/semodule', '-l', 'full').stdout
        if re.search(r'\bliner_notes\b', existing):
            raise ValueError('Existing Liner Notes policy must not be replaced by this isolated test')
        for path in ('/usr/share/selinux/devel/Makefile', '/usr/bin/gcc', '/usr/bin/make'):
            if not Path(path).exists():
                raise ValueError('Missing prerequisite: ' + path)
        for variants in (('lrz', 'rz'), ('lsz', 'sz')):
            if not any(shutil.which(name, path=ENV['PATH']) for name in variants):
                raise ValueError('Real lrzsz must be installed before acceptance')
        command('/usr/bin/python3', '-I', '-c', 'import aiohttp,cryptography')
        work = Path(tempfile.mkdtemp(prefix=sha[:12] + '-', dir=BASE))
        source = work / 'source'
        source.mkdir(mode=0o700)
        content = get('https://codeload.github.com/' + REPOSITORY + '/tar.gz/' + sha,
                      128 * 1024 * 1024)
        extract(content, source, sha)
        # Fixed commands against reviewed, root-protected source, not CI-user input.
        print(command('/usr/bin/make', '-j2', 'all', cwd=source,
                      timeout=600).stdout, flush=True)
        policy = source / 'linux/selinux'
        print(command('/usr/bin/make', '-f', '/usr/share/selinux/devel/Makefile',
                      'liner_notes.pp', cwd=policy).stdout, flush=True)
        if sha != current_main():
            raise ValueError('Main advanced during preparation; no policy was loaded')
        installed = False
        try:
            command('/usr/sbin/semodule', '-X', '200', '-i', policy / 'liner_notes.pp')
            installed = True
            log = work / 'acceptance.log'
            try:
                with log.open('x', encoding='utf8') as stream:
                    result = subprocess.run(['/usr/bin/python3', '-I', str(source / 'ci/selinux_native.py'),
                                             '--designated-ci-vm'], cwd=source, env=ENV,
                                            timeout=1200, stdout=stream, stderr=subprocess.STDOUT, text=True)
            finally:
                # Preserve partial output even if the acceptance process times out.
                if log.is_file():
                    print(log.read_text(encoding='utf8', errors='replace'), flush=True)
            result.check_returncode()
            print(json.dumps({'source_sha': sha, 'archive_sha256': hashlib.sha256(content).hexdigest(),
                              'policy_sha256': hashlib.sha256((policy/'liner_notes.pp').read_bytes()).hexdigest(),
                              'runtime_sha256': hashlib.sha256((source/'runtime').read_bytes()).hexdigest(),
                              'enforcing': True, 'acceptance': 'success'}), flush=True)
        finally:
            # Do not unload a policy while any test child remains in its domain.
            domains = command('/usr/bin/ps', '-eZ').stdout
            if re.search(r':liner_notes(?:_(?:web|link|admin|door|transfer))?_t:', domains):
                raise RuntimeError('Confined process remains: keep policy and evidence for recovery')
            if installed:
                command('/usr/sbin/semodule', '-X', '200', '-r', 'liner_notes')


if __name__ == '__main__':
    main()
