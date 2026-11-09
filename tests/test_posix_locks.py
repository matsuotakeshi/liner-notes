"""Native flock and Solaris-compatible process leases, without private hosts."""
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import posix_locks as locks


def child_mode():
    mode, path, backend = sys.argv[2:5]
    locks.RECORD_LOCKS = backend == 'record'
    if mode == 'game':
        print('READY', flush=True)
        sys.stdin.buffer.read(1)
        os.close(int(path))
        return
    lease = locks.file_lock(path)
    try:
        game = subprocess.Popen([sys.executable, __file__, '--child', 'game',
                                 str(lease.inheritable_fd), backend],
                                pass_fds=(lease.inheritable_fd,), start_new_session=True)
        game.wait()
    finally:
        lease.close()


@unittest.skipUnless(os.name == 'posix', 'POSIX descriptors required')
class LockTests(unittest.TestCase):
    def modes(self):
        return (True,) if sys.platform.startswith('sunos') else (False, True)

    def test_exclusion_unrelated_close_and_immediate_reentry(self):
        for record in self.modes():
            with self.subTest(record=record), tempfile.TemporaryDirectory() as tmp, patch.object(locks, 'RECORD_LOCKS', record):
                path = Path(tmp) / 'lock'
                for _ in range(5):
                    with locks.file_lock(path):
                        fd = os.open(path, os.O_RDONLY)
                        os.close(fd)
                        with self.assertRaises(BlockingIOError):
                            locks.file_lock(path)
                locks.reap()
                self.assertFalse(locks._holders)

    def test_thread_waiter(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(locks, 'RECORD_LOCKS', True):
            path = Path(tmp) / 'lock'
            lease = locks.file_lock(path)
            entered = threading.Event()
            def wait():
                with locks.file_lock(path, nonblocking=False):
                    entered.set()
            worker = threading.Thread(target=wait)
            worker.start()
            try:
                self.assertFalse(entered.wait(0.15))
            finally:
                lease.close()
            worker.join(5)
            self.assertFalse(worker.is_alive())
            self.assertTrue(entered.is_set())

    def test_legacy_record_lock_interoperability(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(locks, 'RECORD_LOCKS', True):
            path = Path(tmp) / 'lock'
            path.touch(mode=0o600)
            code = ('import fcntl,sys; f=open(sys.argv[1],"r+b");'
                    'fcntl.lockf(f,fcntl.LOCK_EX|fcntl.LOCK_NB);'
                    'print("READY",flush=True);sys.stdin.buffer.read(1)')
            old = subprocess.Popen([sys.executable, '-c', code, str(path)], stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                self.assertTrue(select.select([old.stdout], [], [], 5)[0])
                self.assertEqual(old.stdout.readline(), b'READY\n')
                with self.assertRaises(BlockingIOError):
                    locks.file_lock(path)
                old.stdin.write(b'X'); old.stdin.flush()
                self.assertEqual(old.wait(timeout=5), 0)
                with locks.file_lock(path):
                    result = subprocess.run([sys.executable, '-c', code, str(path)],
                                            input=b'X', capture_output=True, timeout=5)
                    self.assertNotEqual(result.returncode, 0)
            finally:
                if old.poll() is None:
                    old.kill(); old.wait()
                old.stdin.close(); old.stdout.close(); old.stderr.close()

    def test_waiting_holder_exits_on_lease_eof(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(locks, 'RECORD_LOCKS', True):
            path = Path(tmp) / 'lock'
            with locks.file_lock(path):
                fd = os.open(path, os.O_RDWR)
                watch, lease = os.pipe()
                answer, ack = os.pipe()
                child = subprocess.Popen([sys.executable, '-I', '-B', str(ROOT / 'scripts/posix_locks.py'),
                                          '--hold', str(fd), str(watch), str(ack), '0'],
                                         pass_fds=(fd, watch, ack), start_new_session=True)
                os.close(watch); os.close(ack); os.close(fd)
                try:
                    os.close(lease)
                    self.assertEqual(child.wait(timeout=5), 0)
                    self.assertEqual(os.read(answer, 4), b'')
                finally:
                    os.close(answer)
                    if child.poll() is None:
                        child.kill(); child.wait()

    def test_owner_group_death_preserves_explicit_exec_lease(self):
        for record in self.modes():
            with self.subTest(record=record), tempfile.TemporaryDirectory() as tmp, patch.object(locks, 'RECORD_LOCKS', record):
                path = Path(tmp) / 'lock'
                owner = subprocess.Popen([sys.executable, __file__, '--child', 'owner', str(path),
                                          'record' if record else 'native'],
                                         stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                         stderr=subprocess.PIPE, start_new_session=True)
                try:
                    self.assertTrue(select.select([owner.stdout], [], [], 5)[0])
                    self.assertEqual(owner.stdout.readline(), b'READY\n')
                    os.killpg(owner.pid, signal.SIGKILL)
                    self.assertEqual(owner.wait(timeout=5), -signal.SIGKILL)
                    for _ in range(5):
                        with self.assertRaises(BlockingIOError):
                            locks.file_lock(path)
                        time.sleep(0.02)
                    owner.stdin.write(b'X'); owner.stdin.flush()
                    deadline = time.monotonic() + 5
                    while True:
                        try:
                            lease = locks.file_lock(path)
                            lease.close()
                            break
                        except BlockingIOError:
                            if time.monotonic() >= deadline:
                                raise
                            time.sleep(0.02)
                finally:
                    owner.stdin.close()
                    if owner.poll() is None:
                        os.killpg(owner.pid, signal.SIGKILL); owner.wait()
                    owner.stdout.close(); owner.stderr.close()

    def test_namespace_metadata_validation_and_stable_inode(self):
        import transfer_spool
        with tempfile.TemporaryDirectory() as tmp, patch.object(locks, 'RECORD_LOCKS', True):
            root = Path(tmp)
            marker = root / locks.NAMESPACE_FILE
            outside = root / 'outside'
            outside.write_bytes(b'untouched')
            outside.chmod(0o600)
            for kind in ('symlink', 'hardlink', 'directory', 'public', 'payload'):
                with self.subTest(kind=kind):
                    if kind == 'symlink': marker.symlink_to(outside)
                    elif kind == 'hardlink': os.link(outside, marker)
                    elif kind == 'directory': marker.mkdir()
                    else:
                        marker.touch(mode=0o600)
                        if kind == 'public': marker.chmod(0o666)
                        else: marker.write_bytes(b'payload')
                    with self.assertRaises((OSError, ValueError)):
                        locks.namespace_lock(root)
                    if marker.is_dir() and not marker.is_symlink(): marker.rmdir()
                    else: marker.unlink()
                    self.assertEqual(outside.read_bytes(), b'untouched')
            outside.unlink()
            with locks.namespace_lock(root):
                inode = marker.stat().st_ino
            self.assertEqual(transfer_spool.manage(root, 'list'), [])
            for action in ('remove', 'restore'):
                with self.assertRaises(ValueError):
                    transfer_spool.manage(root, action, locks.NAMESPACE_FILE, True)
            self.assertEqual(marker.stat().st_ino, inode)
            self.assertEqual(list(root.iterdir()), [marker])

    def test_recovery_cleanup_and_protocol_name_do_not_replace_namespace(self):
        from test_zmodem_recovery import run, Ledger
        from test_zmodem_workflows import spool_contents
        from zmodem_connector import staged_file, TransferFailure
        import vault_zmodem
        with patch.object(locks, 'RECORD_LOCKS', True):
            run()  # Existing retry/metadata/active-lock/retention assertions.
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                with locks.namespace_lock(root):
                    inode = (root / locks.NAMESPACE_FILE).stat().st_ino
                class ReservedName:
                    def receive(self, directory, _input, _output, authorize):
                        authorize()
                        path = directory / locks.NAMESPACE_FILE
                        path.write_bytes(b'payload')
                        return staged_file(directory, required=True)
                member = Ledger()
                with self.assertRaises(TransferFailure):
                    vault_zmodem.runtime_receive(member, ReservedName(), 'host', 2222,
                                                'alice', root, description='test')
                self.assertEqual(member.records, {})
                self.assertEqual(spool_contents(root), [])
                self.assertEqual((root / locks.NAMESPACE_FILE).stat().st_ino, inode)


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--child':
        child_mode()
    else:
        unittest.main()
