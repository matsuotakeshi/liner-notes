"""Reserved UID lifecycle for a future Solaris authenticated Door supervisor.

The supervising caller owns the stop operation. A crashed supervisor's durable
reservation is deliberately retained even when a later UID snapshot is empty.
"""
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import time

from door_broker import secure_path
from posix_locks import file_lock


class SlotBusy(ValueError):
    pass


_PS = ('/usr/bin/ps', '-e', '-o', 'uid=', '-o', 'ruid=', '-o', 'suid=', '-o', 's=')
_ROW = re.compile(r'([0-9]+|[-?]) +([0-9]+|[-?]) +([0-9]+|[-?]) +([A-Z])')


def snapshot_contains(output, uid):
    rows = output.splitlines()
    if not rows:
        raise OSError('Empty Solaris UID snapshot')
    found = False
    for line in rows:
        match = _ROW.fullmatch(line.strip())
        if match is None:
            raise OSError('Incomplete Solaris UID snapshot')
        effective, real, saved, state = match.groups()
        if state == 'Z':
            # ps can print '?' for the saved UID of a zombie.
            continue
        ids = (effective,real,saved)
        if '-' in ids or '?' in ids:
            raise OSError('Solaris live UID not visible')
        if str(uid) in ids:
            found = True
    return found


def _active(uid):
    result = subprocess.run(_PS,capture_output=True,text=True,timeout=5,check=True,
                            env={'PATH':'/usr/bin:/usr/sbin','LC_ALL':'C'})
    if result.stderr:
        raise OSError('Solaris UID snapshot reported an error')
    return snapshot_contains(result.stdout,uid)


class Reservation:
    def __init__(self, lease, uid):
        self._lease = lease
        self.uid = uid

    def close(self):
        """Abandon ownership and leave the durable marker for inspection."""
        if self._lease is not None:
            self._lease.close()
            self._lease = None

    def stop_and_release(self, stop_owned_scope, *, settle=0):
        """Release only after the owned contract is stopped and the UID is gone."""
        if self._lease is None:
            raise ValueError('Reservation already closed')
        stop_owned_scope()
        deadline = time.monotonic() + settle
        while _active(self.uid):
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise SlotBusy('A process still holds the Solaris slot UID')
            time.sleep(min(remaining, 0.05))
        os.ftruncate(self._lease.fd,0)
        os.fsync(self._lease.fd)
        self.close()


def reserve(state_dir, uid):
    if not sys.platform.startswith('sunos') or os.getuid()!=0 or os.geteuid()!=0:
        raise OSError('Solaris root supervisor required')
    if type(uid) is not int or not 0<uid<2**31:
        raise ValueError('Invalid Solaris slot UID')
    directory = secure_path(Path(state_dir),0,directory=True)
    dir_fd = os.open(directory,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
    lease = None
    try:
        if os.fstat(dir_fd).st_mode&0o077:
            raise ValueError('Private Solaris slot directory required')
        try:
            lease = file_lock(directory/('slot-'+str(uid)))
        except BlockingIOError as error:
            raise SlotBusy('Solaris slot already locked') from error
        info = os.fstat(lease.fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid!=0 or info.st_nlink!=1 or info.st_mode&0o077:
            raise ValueError('Unsafe Solaris slot file')
        if info.st_size or _active(uid):
            raise SlotBusy('Solaris slot requires cleanup')
        marker = b'BUSY\n'
        if os.write(lease.fd,marker)!=len(marker):
            raise OSError('Incomplete Solaris slot reservation')
        os.fsync(lease.fd)
        os.fsync(dir_fd)
        result = Reservation(lease,uid)
        lease = None
        return result
    finally:
        if lease is not None:
            lease.close()
        os.close(dir_fd)
