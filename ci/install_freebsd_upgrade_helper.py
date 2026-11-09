#!/usr/local/bin/python3.12 -I
"""First installation only; equal bytes/modes are idempotent, changes refused."""
import importlib.util
import os
from pathlib import Path
import re
import stat
import sys
import tempfile


def trusted(path):
    for item in (path, *path.parents):
        info = item.lstat()
        if stat.S_ISLNK(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError('Root-owned, non-writable source/destination ancestry required')


def directory(path, mode):
    if path.exists() or path.is_symlink():
        trusted(path)
        if not path.is_dir() or stat.S_IMODE(path.stat().st_mode) != mode:
            raise ValueError('Existing directory mode/type differs; initial setup will not change it')
    else:
        trusted(path.parent)
        path.mkdir(mode=mode)
        path.chmod(mode)  # only the directory exclusively created above


def install_file(path, content, mode):
    if path.exists() or path.is_symlink():
        trusted(path)
        if not path.is_file() or stat.S_IMODE(path.stat().st_mode) != mode or path.read_bytes() != content:
            raise ValueError('Existing helper differs; replacement requires separate reviewed approval')
        return False
    trusted(path.parent)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, mode)
    with os.fdopen(fd, 'wb') as output:
        output.write(content)
        output.flush()
        os.fchmod(output.fileno(), mode)
        os.fsync(output.fileno())
    return True


def main():
    if os.geteuid() != 0 or not sys.platform.startswith('freebsd'):
        raise ValueError('FreeBSD root setup required')
    if len(sys.argv) != 2 or not re.fullmatch(r'[0-9a-f]{40}', sys.argv[1]):
        raise ValueError('Exact reviewed main SHA required')
    sha = sys.argv[1]
    source = Path(__file__).resolve().parents[1]
    names = ('freebsd_upgrade_root.py', 'selinux_root.py', 'install_freebsd_upgrade_helper.py', 'install-freebsd-upgrade-helper.sh')
    for name in names:
        trusted(source/'ci'/name)
    spec = importlib.util.spec_from_file_location('root_source', source/'ci/selinux_root.py')
    acquisition = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(acquisition)
    os.environ.clear()
    os.environ.update(acquisition.ENV)
    os.umask(0o077)
    if not re.search(r'\binet 10\.0\.0\.50\b', acquisition.command('/sbin/ifconfig', '-a').stdout):
        raise ValueError('Designated FreeBSD test VM only')
    if acquisition.current_main() != sha:
        raise ValueError('Main advanced; review current source before setup')
    with tempfile.TemporaryDirectory(prefix='liner-helper-reviewed-', dir='/var/tmp') as temporary:
        verified = Path(temporary)
        acquisition.extract(acquisition.get('https://codeload.github.com/'+acquisition.REPOSITORY+'/tar.gz/'+sha, 128*1024**2), verified, sha)
        for name in names:
            if (verified/'ci'/name).read_bytes() != (source/'ci'/name).read_bytes():
                raise ValueError('Setup source differs from reviewed main')
        if acquisition.current_main() != sha:
            raise ValueError('Main advanced before installation')
        # Prevalidate every existing object before the first mutation. Initial
        # setup cannot silently replace an older root helper or adjust modes.
        targets = [(Path('/usr/local/libexec/liner-notes-freebsd-upgrade-ci'), 'freebsd_upgrade_root.py', 0o755),
                   (Path('/usr/local/libexec/liner-notes-ci/selinux_root.py'), 'selinux_root.py', 0o644)]
        for path, name, mode in targets:
            if path.exists() or path.is_symlink():
                trusted(path)
                if not path.is_file() or stat.S_IMODE(path.stat().st_mode) != mode or path.read_bytes() != (verified/'ci'/name).read_bytes():
                    raise ValueError('Existing root helper/module differs; no files were replaced')
        directories = [(Path('/usr/local/libexec/liner-notes-ci'), 0o755), (Path('/var/db/liner-notes-ci'), 0o700)]
        for path, mode in directories:
            if path.exists() or path.is_symlink():
                trusted(path)
                if not path.is_dir() or stat.S_IMODE(path.stat().st_mode) != mode:
                    raise ValueError('Existing setup directory differs; no modes were changed')
            else:
                trusted(path.parent)
        for path, mode in directories:
            directory(path, mode)
        for path, name, mode in targets:
            install_file(path, (verified/'ci'/name).read_bytes(), mode)
    print('Initial helpers verified/installed only; no sudo rule, package or service was changed.')


if __name__ == '__main__':
    main()
