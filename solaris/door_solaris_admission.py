"""Fail-closed launch admission for the Solaris Door supervisor candidate."""

from contextlib import contextmanager
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys

from door_broker import secure_path
from door_solaris_slot import reserve


def _root_path(path):
    value = os.fspath(path)
    if not isinstance(value, str) or not re.fullmatch(r"/[A-Za-z0-9_./-]+", value):
        raise ValueError("Invalid Solaris Door execution root")
    parts = PurePosixPath(value)
    if parts.anchor != "/" or str(parts) != value or ".." in parts.parts:
        raise ValueError("Non-canonical Solaris Door execution root")
    return value


def _verify_mount(table, root):
    entry = None
    for line in table.split("\n"):
        if not line:
            continue
        fields = line.split("\t")
        if len(fields) != 5:
            raise ValueError("Invalid Solaris mount table")
        source, point, fs_type, options, _ = fields
        if point.startswith(root + "/"):
            raise ValueError("Nested mount in Solaris Door execution root")
        if point == root:
            if entry is not None or (source, fs_type) != ("swap", "tmpfs"):
                raise ValueError("Unexpected Solaris Door execution mount")
            entry = set(options.split(","))
    if entry is None or not ("nosuid" in entry or {"nosetuid", "nodevices"} <= entry):
        raise ValueError("Solaris Door execution mount needs nosuid and nodevices")
    if {"suid", "setuid", "devices", "noexec"} & entry:
        raise ValueError("Conflicting Solaris Door execution mount options")


def _verify_root(root, root_fd):
    opened = os.fstat(root_fd)
    if not stat.S_ISDIR(opened.st_mode) or opened.st_uid != 0 or opened.st_mode & 0o022:
        raise ValueError("Unsafe Solaris Door execution directory")
    if not os.fstatvfs(root_fd).f_flag & os.ST_NOSUID:
        raise ValueError("Solaris execution root descriptor is missing nosuid")
    _verify_mount(Path("/etc/mnttab").read_text(encoding="utf-8"), root)
    current = os.stat(root, follow_symlinks=False)
    if not stat.S_ISDIR(current.st_mode) or not os.path.samestat(opened, current):
        raise ValueError("Solaris Door execution root changed during admission")


@contextmanager
def admit(root, state_dir, uid):
    """Keep the validated root FD and reservation open for the launch owner.

    Only the owner may release the reservation after verifying scope cleanup.
    Leaving without an explicit release retains the durable BUSY marker.
    """
    if not sys.platform.startswith("sunos") or os.getuid() != 0 or os.geteuid() != 0:
        raise OSError("Solaris root supervisor required")
    root = _root_path(root)
    secure_path(root, 0, directory=True)
    root_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)
    try:
        _verify_root(root, root_fd)
        reservation = reserve(state_dir, uid)
        try:
            yield root_fd, reservation
        finally:
            reservation.close()
    finally:
        os.close(root_fd)
