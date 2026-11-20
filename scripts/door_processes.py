"""OS-specific lifetime/privilege barriers for isolated Door workers.

FreeBSD ABI: sys/sys/procctl.h and sys/sys/_types.h (13+).
Only the calling worker's reaper subtree is signalled, never a ps-derived PID.
Solaris deliberately fails closed until a contract/privilege backend is tested.
"""
import ctypes
import errno
import os
from pathlib import Path
import signal
import subprocess
import sys
import time


def supported():
    return (sys.platform.startswith('freebsd') or
            (sys.platform.startswith('linux') and hasattr(os, 'pidfd_open')
             and hasattr(signal, 'pidfd_send_signal')))


def processes(uid, terminate=False, sig=None):
    if sys.platform.startswith('freebsd'):
        if terminate:
            raise OSError('FreeBSD signalling requires an owned reaper scope')
        # Read only. Saved/real/effective IDs all reserve a dedicated slot.
        # FreeBSD consumes everything after '=' as that column's header.
        # Repeat -o so empty headers do not swallow the remaining UID columns.
        result = subprocess.run(['/bin/ps', '-ax', '-o', 'uid=', '-o', 'ruid=', '-o', 'svuid='],
                                env={'PATH': '/bin:/usr/bin', 'LC_ALL': 'C'},
                                capture_output=True, text=True, timeout=5, check=True)
        rows = [line.split() for line in result.stdout.splitlines() if line.strip()]
        if not rows or any(len(row) != 3 or not all(v.isdecimal() for v in row) for row in rows):
            raise OSError('Cannot establish slot UID availability')
        return any(uid in map(int, row) for row in rows)
    if not sys.platform.startswith('linux'):
        raise OSError('Process isolation unsupported on this OS')
    if sig is None:sig=signal.SIGKILL
    active = False
    for entry in Path('/proc').iterdir():
        if not entry.name.isdecimal():
            continue
        handle = None
        try:
            try:
                handle = os.pidfd_open(int(entry.name))
            except OSError as error:
                if error.errno == errno.EINVAL:
                    # A process can disappear between /proc enumeration and
                    # pidfd acquisition. Only a missing status permits the
                    # existing FileNotFoundError path below; a present process
                    # or an unreadable status must still fail closed.
                    (entry/'status').stat()
                raise
            status = (entry/'status').read_text()
            values = next(line for line in status.splitlines() if line.startswith('Uid:')).split()[1:]
            if uid not in map(int, values):
                continue
            if next(line for line in status.splitlines() if line.startswith('State:')).split()[1] == 'Z':
                continue
            active = True
            if terminate:
                signal.pidfd_send_signal(handle, sig)
        except (ProcessLookupError, FileNotFoundError):
            pass
        finally:
            if handle is not None:
                os.close(handle)
    return active


class ReaperStatus(ctypes.Structure):
    _fields_ = [('flags', ctypes.c_uint), ('children', ctypes.c_uint),
                ('descendants', ctypes.c_uint), ('reaper', ctypes.c_int),
                ('pid', ctypes.c_int), ('reserved', ctypes.c_uint * 15)]


class ReaperKill(ctypes.Structure):
    _fields_ = [('signal', ctypes.c_int), ('flags', ctypes.c_uint),
                ('subtree', ctypes.c_int), ('killed', ctypes.c_uint),
                ('failed_pid', ctypes.c_int), ('reserved', ctypes.c_uint * 15)]


def procctl(command, value=None):
    libc = ctypes.CDLL(None, use_errno=True)
    call = libc.procctl
    call.argtypes = [ctypes.c_int, ctypes.c_int64, ctypes.c_int, ctypes.c_void_p]
    call.restype = ctypes.c_int
    if call(os.P_PID, os.getpid(), command, ctypes.byref(value) if value is not None else None):
        number = ctypes.get_errno()
        raise OSError(number, 'procctl rejected')


def no_new_privileges():
    if sys.platform.startswith('freebsd'):
        procctl(19, ctypes.c_int(1))  # PROC_NO_NEW_PRIVS_CTL / ENABLE
        status = ctypes.c_int()
        procctl(20, status)
        if status.value != 1:
            raise OSError('no-new-privileges verification failed')
    elif sys.platform.startswith('linux'):
        if ctypes.CDLL(None, use_errno=True).prctl(38, 1, 0, 0, 0):
            raise OSError('no-new-privileges unavailable')
    else:
        raise OSError('Privilege isolation unsupported on this OS')


class Scope:
    """Created in a single-use supervisor child before it forks any Door."""
    def __init__(self, uid):
        self.uid = uid
        self.freebsd = sys.platform.startswith('freebsd')
        if self.freebsd:
            procctl(2)  # PROC_REAP_ACQUIRE
        elif sys.platform.startswith('linux'):
            if ctypes.CDLL(None, use_errno=True).prctl(36, 1, 0, 0, 0):
                raise OSError('subreaper unavailable')
        else:
            raise OSError('Process isolation unsupported on this OS')

    def active(self):
        if not self.freebsd:
            return processes(self.uid)
        status = ReaperStatus()
        procctl(4, status)
        if not status.flags & 1 or status.reaper != os.getpid():
            raise OSError('Reaper ownership lost')
        return bool(status.descendants)

    def signal(self, sig):
        if not self.freebsd:
            processes(self.uid, terminate=True, sig=sig)
            return
        try:
            procctl(6, ReaperKill(signal=sig))  # all descendants, kernel-held identities
        except OSError as error:
            if error.errno != errno.ESRCH:
                raise

    def reap(self):
        while True:
            try:
                child, _ = os.waitpid(-1, os.WNOHANG)
                if not child:
                    return
            except ChildProcessError:
                return

    def stop(self):
        self.signal(signal.SIGHUP)
        time.sleep(.25)
        # Never release the durable slot reservation before cleanup completes.
        while True:
            self.signal(signal.SIGKILL)
            self.reap()
            if not self.active():
                self.reap()
                return
            time.sleep(.05)
