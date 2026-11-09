"""Narrow DAC handling for disposable FreeBSD package fixtures only."""
from pathlib import Path
import stat


def package_install_argv(argv):
    """Use a public creation mask only in the fixed package-install child.

    Callers supply reviewed pkg commands, never an untrusted shell string.
    The parent process and subsequent fixture data retain their private mask.
    """
    return ['/bin/sh', '-c', 'umask 022; exec "$@"',
            'liner-notes-package-install', *map(str, argv)]


def repair_legacy_sbin(root):
    """Repair the one observed legacy bootstrap directory in a NEW jail copy.

    Never recurse or touch configurations, data, keys, files, or host parents.
    The jail entrypoint must complete its root/jail/exact-path admission first.
    """
    root = Path(root)
    if not root.is_absolute():
        raise ValueError('Absolute jail root required')
    directories = [root, root/'usr', root/'usr/local', root/'usr/local/sbin']
    for directory in directories:
        info = directory.lstat()
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError('Public fixture directory must be root-owned and non-writable')
    mode = stat.S_IMODE(directories[-1].lstat().st_mode)
    if mode == 0o700:
        directories[-1].chmod(0o755)
        return ['usr/local/sbin']
    if mode not in (0o755, 0o555):
        raise ValueError('Unexpected public fixture directory mode')
    return []
