"""Bounded interruption cleanup wait, using a fake clock and fake lock lease."""
import contextlib
import errno
import pathlib
import tempfile
import unittest
from unittest.mock import patch
import test_zmodem_batch_resume as target

class Clock:
    def __init__(self, callback=lambda: None):
        self.now=0;self.callback=callback
    def monotonic(self):return self.now
    def sleep(self, seconds):
        self.now+=seconds;self.callback()

class CleanupWait(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=pathlib.Path(self.tmp.name);self.data=self.root/'pending-test'/'data'
        self.data.mkdir(parents=True);(self.data/'one.bin').write_bytes(b'one')
    def test_ready_returns_immediately(self):
        clock=Clock()
        with patch.object(target,'time',clock),patch.object(target,'file_lock',return_value=contextlib.nullcontext()) as lock:
            self.assertEqual(target.wait_for_partials(self.root,['one.bin']),[self.data/'one.bin'])
        self.assertEqual(clock.now,0)
        lock.assert_called_once_with(self.data.parent/'lock',create=False)
    def test_waits_for_delayed_file(self):
        clock=Clock(lambda: (self.data/'two.bin').write_bytes(b'two') if clock.now>=2 else None)
        with patch.object(target,'time',clock),patch.object(target,'file_lock',side_effect=lambda *a,**k:contextlib.nullcontext()):
            self.assertEqual(len(target.wait_for_partials(self.root,['one.bin','two.bin'])),2)
        self.assertGreaterEqual(clock.now,2)
    def test_waits_for_receiver_lock(self):
        clock=Clock()
        def lease(*a,**k):
            if clock.now<3:raise BlockingIOError(errno.EAGAIN,'busy')
            return contextlib.nullcontext()
        with patch.object(target,'time',clock),patch.object(target,'file_lock',side_effect=lease):
            target.wait_for_partials(self.root,['one.bin'])
        self.assertGreaterEqual(clock.now,3)
    def test_missing_or_extra_files_time_out_with_diagnostics(self):
        for expected in (['one.bin','two.bin'],['two.bin']):
            clock=Clock()
            with patch.object(target,'time',clock),patch.object(target,'file_lock') as lock:
                with self.assertRaisesRegex(AssertionError,'observed=.*one.bin.*3.*file set mismatch'):
                    target.wait_for_partials(self.root,expected,timeout=1)
            self.assertEqual(clock.now,1);lock.assert_not_called()
    def test_busy_lock_times_out(self):
        clock=Clock()
        with patch.object(target,'time',clock),patch.object(target,'file_lock',side_effect=BlockingIOError(errno.EAGAIN,'busy')):
            with self.assertRaisesRegex(AssertionError,'lock/file unavailable'):
                target.wait_for_partials(self.root,['one.bin'],timeout=1)
        self.assertEqual(clock.now,1)
    def test_unexpected_io_error_is_not_retried(self):
        clock=Clock()
        with patch.object(target,'time',clock),patch.object(target,'file_lock',side_effect=OSError(errno.EIO,'disk')):
            with self.assertRaises(OSError):target.wait_for_partials(self.root,['one.bin'])
        self.assertEqual(clock.now,0)

if __name__=='__main__':unittest.main()
