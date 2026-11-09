"""Native flock, or a Solaris record-lock holder with a pipe lease.

Solaris' Python flock emulation is process-owned and cannot lock directories.
Keep existing flock behavior elsewhere. The fixed Solaris namespace file is
infrastructure, never a transfer payload, and must not be unlinked while in use.
"""
import errno
import os
from pathlib import Path
import select
import stat
import struct
import subprocess
import sys
import threading

if os.name == 'posix':
    import fcntl
else:
    fcntl = None

RECORD_LOCKS = sys.platform.startswith('sunos')
NAMESPACE_FILE = '.namespace-lock'
_holders = []
_guard = threading.Lock()


def _after_fork():
    global _holders, _guard
    _holders, _guard = [], threading.Lock()


if hasattr(os, 'register_at_fork'):
    os.register_at_fork(after_in_child=_after_fork)


def reap():
    with _guard:
        _holders[:] = [child for child in _holders if child.poll() is None]


class Lease:
    def __init__(self, fd, pipe=None, child=None):
        self.fd, self.pipe, self.child = fd, pipe, child

    @property
    def inheritable_fd(self):
        """Explicit pass_fds target when a child must retain the lease."""
        return self.fd if self.pipe is None else self.pipe

    def close(self):
        if self.fd is None:
            return
        fd, self.fd = self.fd, None
        os.close(fd)
        if self.pipe is not None:
            os.close(self.pipe)
            self.pipe = None
            # Normally the holder exits immediately. An explicitly inherited
            # child lease must continue holding the lock after this close.
            try:
                self.child.wait(timeout=0.2)
            except subprocess.TimeoutExpired:
                with _guard:
                    _holders.append(self.child)
        reap()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def _record_lease(fd, nonblocking):
    reap()
    watch, lease = os.pipe()
    try:
        answer, ack = os.pipe()
    except BaseException:
        os.close(watch); os.close(lease)
        raise
    child = None
    try:
        child = subprocess.Popen(
            [sys.executable, '-I', '-B', str(Path(__file__).resolve()), '--hold',
             str(fd), str(watch), str(ack), str(int(nonblocking))],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            close_fds=True, pass_fds=(fd, watch, ack), start_new_session=True)
        os.close(watch); watch = None
        os.close(ack); ack = None
        data = b''
        while len(data) < 4:
            part = os.read(answer, 4 - len(data))
            if not part:
                raise OSError(errno.EIO, 'Lock holder ended before acquisition')
            data += part
        error = struct.unpack('!I', data)[0]
        if error:
            raise OSError(error, os.strerror(error))
        return Lease(fd, lease, child)
    except BaseException:
        os.close(lease)
        if child is not None:
            if child.poll() is None:
                child.terminate()
            try:
                child.wait(timeout=2)
            except subprocess.TimeoutExpired:
                child.kill(); child.wait()
        raise
    finally:
        for end in (watch, answer, ack):
            if end is not None:
                os.close(end)


def _acquire(fd, nonblocking):
    if RECORD_LOCKS:
        return _record_lease(fd, nonblocking)
    fcntl.flock(fd, fcntl.LOCK_EX | (fcntl.LOCK_NB if nonblocking else 0))
    return Lease(fd)


def file_lock(path, *, create=True, nonblocking=True):
    fd = os.open(path, os.O_RDWR | os.O_NOFOLLOW | (os.O_CREAT if create else 0), 0o600)
    try:
        if RECORD_LOCKS:
            _private_file(fd)
        return _acquire(fd, nonblocking)
    except BaseException:
        os.close(fd)
        raise


def _private_file(fd):
    info = os.fstat(fd)
    if (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_mode & 0o077
            or (os.geteuid() != 0 and info.st_uid != os.geteuid())):
        raise ValueError('Unsafe private lock file')


def namespace_lock(root):
    fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        if not RECORD_LOCKS:
            result = _acquire(fd, False)
            fd = None
            return result
        info = os.fstat(fd)
        if info.st_mode & 0o022 or (os.geteuid() != 0 and info.st_uid != os.geteuid()):
            raise ValueError('Unsafe spool namespace directory')
        lock = os.open(NAMESPACE_FILE, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW,
                       0o600, dir_fd=fd)
        try:
            _private_file(lock)
            if os.fstat(lock).st_size:
                raise ValueError('Namespace lock must not contain payload data')
            return _acquire(lock, False)
        except BaseException:
            os.close(lock)
            raise
    finally:
        if fd is not None:
            os.close(fd)


def _hold(fd, watch, answer, nonblocking):
    """Fresh interpreter: no Python work in a multithreaded post-fork child."""
    try:
        while True:
            try:
                fcntl.lockf(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError as error:
                if error.errno not in (errno.EAGAIN, errno.EACCES) or nonblocking:
                    os.write(answer, struct.pack('!I', errno.EAGAIN if error.errno == errno.EACCES else error.errno))
                    return
                ready, _, _ = select.select([watch], [], [], 0.1)
                if ready and not os.read(watch, 1):
                    return  # Parent disappeared while waiting for another lock.
        os.write(answer, struct.pack('!I', 0))
        while os.read(watch, 1):
            pass
    finally:
        os.close(fd)
        os.close(answer)
        os.close(watch)


if __name__ == '__main__':
    if len(sys.argv) != 6 or sys.argv[1] != '--hold':
        raise SystemExit('Internal lock-holder invocation required')
    _hold(*(int(value) for value in sys.argv[2:]))
