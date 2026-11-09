"""Failure logs survive private-root cleanup; partial starts are always stopped."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ci"))
from freebsd_fixture_service import RcFixture, fixture_directory, save_failure, LIMIT


class ServiceFixtureTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.work = Path(self.tmp.name)
        self.artifacts = self.work / "artifacts"
        self.artifacts.mkdir()
        self.sha = "a" * 40

    @unittest.skipUnless(os.name == "posix", "POSIX process existence checks")
    def test_partial_start_stops_real_child_and_preserves_log(self):
        calls, child = [], None
        with fixture_directory(self.work) as base:
            (base / "daemon.log").write_text("exact startup diagnostic\n")
            def run(argv, **kwargs):
                nonlocal child
                calls.append(argv[-1])
                if argv[-1] == "start":
                    child = subprocess.Popen([sys.executable, "-c", "import time;time.sleep(60)"])
                    (base / "child.pid").write_text(str(child.pid))
                    return subprocess.CompletedProcess(argv, 1, "", "synthetic rc start error\n")
                child.terminate(); child.wait(timeout=5)
                return subprocess.CompletedProcess(argv, 0)
            fixture = RcFixture("private-rc", {}, base, self.artifacts, self.sha, run)
            try:
                with self.assertRaisesRegex(RuntimeError, "start failed"):
                    with fixture.running("initial"):
                        self.fail("Failed start entered test body")
            finally:
                if child and child.poll() is None: child.kill(); child.wait()
            self.assertEqual(calls, ["start", "stop"])
            self.assertFalse(fixture.cleanup_failed)
        self.assertFalse(base.exists())
        result = json.loads((self.artifacts / "FREEBSD-RC-FAILURE-initial.json").read_text())
        self.assertEqual(result["daemon_log_tail"], "exact startup diagnostic\n")
        self.assertEqual(result["source_sha"], self.sha)
        self.assertIn("synthetic rc start error", result["private_rc_output_tail"])

    def test_assertion_failure_stops_and_success_creates_no_failure_receipt(self):
        for fail in (False, True):
            calls = []
            with fixture_directory(self.work) as base:
                def run(argv, **kwargs):
                    calls.append(argv[-1]); return subprocess.CompletedProcess(argv, 0)
                fixture = RcFixture("private-rc", {}, base, self.artifacts, self.sha, run)
                if fail:
                    with self.assertRaisesRegex(AssertionError, "readback"):
                        with fixture.running("updated"): raise AssertionError("readback")
                else:
                    with fixture.running("initial"): pass
            self.assertEqual(calls, ["start", "status", "stop"])
        self.assertFalse((self.artifacts / "FREEBSD-RC-FAILURE-initial.json").exists())
        self.assertTrue((self.artifacts / "FREEBSD-RC-FAILURE-updated.json").exists())

    def test_timeout_start_still_stops(self):
        calls = []
        with fixture_directory(self.work) as base:
            def run(argv, **kwargs):
                calls.append(argv[-1])
                if argv[-1] == "start": raise subprocess.TimeoutExpired(argv, 40)
                return subprocess.CompletedProcess(argv, 1)
            fixture = RcFixture("private-rc", {}, base, self.artifacts, self.sha, run)
            with self.assertRaises(subprocess.TimeoutExpired):
                with fixture.running("initial"): pass
            self.assertFalse(fixture.cleanup_failed)
        self.assertEqual(calls, ["start", "stop"])

    @unittest.skipUnless(os.name == "posix", "POSIX signal-zero semantics")
    def test_live_pid_after_stop_fails_and_retains_private_data(self):
        with fixture_directory(self.work) as base:
            (base / "child.pid").write_text(str(os.getpid()))
            def run(argv, **kwargs): return subprocess.CompletedProcess(argv, 0)
            fixture = RcFixture("private-rc", {}, base, self.artifacts, self.sha, run)
            with self.assertRaisesRegex(RuntimeError, "remain after stop"):
                with fixture.running("initial"): pass
            self.assertTrue(fixture.cleanup_failed)
        self.assertTrue(base.is_dir())
        self.assertEqual(json.loads((self.artifacts / "FREEBSD-RC-FAILURE-initial.json").read_text())["reason"], "cleanup-failed")

    def test_tail_is_bounded_and_exclusive(self):
        log = self.work / "daemon.log"
        log.write_bytes(b"old" * LIMIT + b"last")
        result = save_failure(log, self.artifacts, "initial", "test", self.sha)
        doc = json.loads(result.read_text())
        self.assertEqual(len(doc["daemon_log_tail"]), LIMIT)
        self.assertTrue(doc["daemon_log_tail"].endswith("last"))
        with self.assertRaises(FileExistsError): save_failure(log, self.artifacts, "initial", "test", self.sha)
        if os.name == "posix": self.assertEqual(result.stat().st_mode & 0o777, 0o600)

    @unittest.skipUnless(os.name == "posix", "POSIX symlinks")
    def test_log_symlink_and_fifo_are_rejected_without_blocking(self):
        outside = self.work / "outside"; outside.write_text("must not copy")
        log = self.work / "daemon.log"; log.symlink_to(outside)
        with self.assertRaises(OSError): save_failure(log, self.artifacts, "initial", "test", self.sha)
        log.unlink(); os.mkfifo(log)
        with self.assertRaises(ValueError): save_failure(log, self.artifacts, "initial", "test", self.sha)
        self.assertFalse(list(self.artifacts.iterdir()))


if __name__ == "__main__": unittest.main()
