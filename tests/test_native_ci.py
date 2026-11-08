#!/usr/bin/env python3
"""PR-safe native orchestration tests. No LAN keys, SSH or production access."""
import hashlib
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ci"))
from native_ci import all_passed, parse_result, require_trusted_main, run_parallel, ssh_options, supervise
from native_remote import RESULT, commands_for, host_lock, unpack

SHA = "a" * 40


class PolicyTests(unittest.TestCase):
    def test_only_exact_trusted_main_can_reach_ssh(self):
        env = dict(GITHUB_REF="refs/heads/main", GITHUB_EVENT_NAME="push",
                   GITHUB_REPOSITORY="matsuotakeshi/liner-notes", GITHUB_SHA=SHA)
        require_trusted_main(env, SHA, SHA)
        require_trusted_main(dict(env, GITHUB_EVENT_NAME="workflow_dispatch"), SHA, SHA)
        for key, bad in (("GITHUB_REF", "refs/pull/1/merge"),
                         ("GITHUB_EVENT_NAME", "pull_request"),
                         ("GITHUB_EVENT_NAME", "pull_request_target"),
                         ("GITHUB_REPOSITORY", "someone/fork"),
                         ("GITHUB_SHA", "b" * 40)):
            with self.subTest(key=key, bad=bad), self.assertRaises(ValueError):
                require_trusted_main(dict(env, **{key: bad}), SHA, SHA)
        with self.assertRaises(ValueError):
            require_trusted_main(env, SHA, "b" * 40)

    def test_host_keys_and_dedicated_keys_are_mandatory(self):
        options = ssh_options("/known/key")
        for value in ("/known/key", "IdentitiesOnly=yes", "BatchMode=yes",
                      "PasswordAuthentication=no", "StrictHostKeyChecking=yes"):
            self.assertIn(value, options)

    def test_native_test_commands_are_preserved(self):
        self.assertEqual(commands_for("freebsd"), [["make", "-j2"], ["make", "check"]])
        for target in ("rocky", "ubuntu"):
            flags = ["CC=clang", "PYTHON=python3", "SQLITE_CFLAGS=", "SQLITE_LIBS=-lsqlite3"]
            self.assertEqual(commands_for(target), [["make", "-j2", *flags], ["make", *flags, "check"]])

    def test_parallel_tasks_overlap_and_failure_does_not_skip_other_hosts(self):
        barrier = threading.Barrier(3)
        intervals = {}
        mutex = threading.Lock()

        def task(target):
            start = time.monotonic()
            barrier.wait(timeout=5)  # Serial scheduling would fail this test.
            time.sleep(0.05)
            with mutex:
                intervals[target] = (start, time.monotonic())
            if target == "rocky":
                raise RuntimeError("Simulated one-host failure")
            return dict(target=target, status="passed", source_sha=SHA)

        targets = ["freebsd", "rocky", "ubuntu"]
        results = run_parallel(targets, task)
        self.assertEqual(set(intervals), set(targets))
        self.assertLess(max(x[0] for x in intervals.values()), min(x[1] for x in intervals.values()))
        self.assertEqual([x["status"] for x in results], ["passed", "failed", "passed"])
        self.assertFalse(all_passed(results, targets, SHA))

    def test_gate_rejects_missing_duplicate_failed_cancelled_and_wrong_sha(self):
        targets = ["freebsd", "rocky", "ubuntu"]
        good = [dict(target=t, source_sha=SHA, status="passed") for t in targets]
        self.assertTrue(all_passed(good, targets, SHA))
        for bad in (good[:-1], good + [good[0]], [good[0], good[0], good[2]]):
            self.assertFalse(all_passed(bad, targets, SHA))
        for patch in ({"status": "failed"}, {"status": "cancelled"}, {"status": "skipped"}, {"source_sha": "b" * 40}):
            self.assertFalse(all_passed([dict(good[0], **patch), *good[1:]], targets, SHA))

    def test_worker_receipt_must_match_exact_os_commit_and_success(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / "worker.log"
            record = dict(target="freebsd", source_sha=SHA, status="passed")
            line = RESULT + json.dumps(record) + "\n"
            log.write_text("test output\n" + line)
            parse_result(log, "freebsd", SHA)
            for contents in ("", line + line, line.replace(SHA, "b" * 40),
                             line.replace("freebsd", "ubuntu"), line.replace("passed", "failed")):
                log.write_text(contents)
                with self.assertRaises((ValueError, RuntimeError)):
                    parse_result(log, "freebsd", SHA)

    def test_workflows_keep_lan_on_main_and_share_exclusion(self):
        folder = ROOT / ".github/workflows"
        names = ["multi-os-ci.yml", "freebsd-build-test.yml", "rocky-build-test.yml", "ubuntu-build-test.yml"]
        for name in names:
            doc = yaml.load((folder / name).read_text(), Loader=yaml.BaseLoader)
            self.assertNotIn("pull_request", doc["on"])
            self.assertEqual(doc["concurrency"], dict(group="liner-notes-native-hosts", **{"cancel-in-progress": "false"}))
            job = doc["jobs"]["native" if name == names[0] else "test"]
            self.assertIn("github.ref == 'refs/heads/main'", job["if"])
            self.assertIn("github.repository == 'matsuotakeshi/liner-notes'", job["if"])
            self.assertEqual(job["runs-on"], ["self-hosted", "Linux", "X64"])
            artifact = job["steps"][-1]
            self.assertEqual(artifact["if"], "${{ always() }}")
            self.assertIn("*.log", artifact["with"]["path"])
            self.assertIn("results.json", artifact["with"]["path"])
        doc = yaml.load((folder / names[0]).read_text(), Loader=yaml.BaseLoader)
        gate = doc["jobs"]["release-gate"]
        self.assertEqual(gate["needs"], ["native"])
        self.assertEqual(gate["if"], "${{ always() }}")
        self.assertIn('test "$NATIVE_RESULT" = success', gate["steps"][0]["run"])
        lint = yaml.load((folder / "native-ci-lint.yml").read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(lint["jobs"]["native-source-tests"]["runs-on"], "ubuntu-24.04")


class ArchiveTests(unittest.TestCase):
    def archive(self, folder, member_name="ci/test.py", link=False):
        path = Path(folder) / "source.tar.gz"
        with tarfile.open(path, "w:gz") as tar:
            info = tarfile.TarInfo(member_name)
            if link:
                info.type = tarfile.SYMTYPE
                info.linkname = "/etc/passwd"
                tar.addfile(info)
            else:
                info.size = 4
                info.mode = 0o755
                tar.addfile(info, io.BytesIO(b"test"))
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def test_archive_checksum_and_non_overwriting_extraction(self):
        with tempfile.TemporaryDirectory() as folder:
            digest = self.archive(folder)
            with self.assertRaises(ValueError):
                unpack(folder, "0" * 64, SHA)
            unpack(folder, digest, SHA)
            self.assertEqual((Path(folder) / "source/ci/test.py").read_bytes(), b"test")
            self.assertEqual((Path(folder) / "source/.ci-source-sha").read_text().strip(), SHA)
            with self.assertRaises(FileExistsError):
                unpack(folder, digest, SHA)

    def test_archive_rejects_traversal_absolute_and_links(self):
        for name, link in (("../escape", False), ("/escape", False), ("link", True)):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as folder:
                digest = self.archive(folder, name, link)
                with self.assertRaises(ValueError):
                    unpack(folder, digest, SHA)


@unittest.skipUnless(os.name == "posix", "Native worker uses POSIX process groups and select on pipes")
class ProcessTests(unittest.TestCase):
    def test_same_host_lock_rejects_overlap_and_releases_after_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "native.lock"
            with self.assertRaisesRegex(RuntimeError, "simulated"):
                with host_lock(path):
                    with self.assertRaises(BlockingIOError), host_lock(path):
                        self.fail("A second run acquired the host lock")
                    raise RuntimeError("simulated worker failure")
            with host_lock(path):
                pass

    def launch(self, commands, timeout=8, lease=2):
        code = ("import sys,json; sys.path.insert(0,sys.argv[1]); from native_remote import run_commands; "
                "run_commands(json.loads(sys.argv[2]),0,float(sys.argv[3]),float(sys.argv[4]))")
        return subprocess.Popen([sys.executable, "-u", "-c", code, str(ROOT / "ci"), json.dumps(commands), str(timeout), str(lease)],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def finish(self, process, expected):
        try:
            process.wait(timeout=12)
            output = process.stdout.read().decode()
            self.assertEqual(process.returncode == 0, expected, output)
            return output
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdout.close()
            try:
                process.stdin.close()
            except BrokenPipeError:
                pass

    def test_success_failure_and_split_heartbeat(self):
        for command, expected in (("import time; time.sleep(.5)", True), ("raise SystemExit(7)", False)):
            process = self.launch([[sys.executable, "-c", command]])
            process.stdin.write(b"al")
            process.stdin.flush()
            time.sleep(0.1)
            process.stdin.write(b"ive\n")
            process.stdin.flush()
            self.finish(process, expected)

    def test_timeout_and_lost_heartbeat_stop_work(self):
        command = [[sys.executable, "-c", "import time; time.sleep(30)"]]
        for timeout, lease, message in ((0.6, 5, "timed out"), (8, 0.6, "heartbeat expired")):
            process = self.launch(command, timeout, lease)
            process.stdin.write(b"alive\n")
            process.stdin.flush()
            self.assertIn(message, self.finish(process, False))

    def test_disconnect_reaps_child_process_group(self):
        with tempfile.TemporaryDirectory() as folder:
            marker = Path(folder) / "child.pid"
            code = ("import subprocess,sys,time; from pathlib import Path; "
                    "p=subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)']); "
                    "Path(sys.argv[1]).write_text(str(p.pid)); time.sleep(30)")
            process = self.launch([[sys.executable, "-c", code, str(marker)]])
            process.stdin.write(b"alive\n")
            process.stdin.flush()
            limit = time.monotonic() + 5
            while not marker.exists() and time.monotonic() < limit:
                time.sleep(0.05)
            self.assertTrue(marker.exists())
            child = int(marker.read_text())
            process.stdin.close()
            self.assertIn("Runner disconnected", self.finish(process, False))
            status = subprocess.run(["ps", "-o", "stat=", "-p", str(child)], capture_output=True, text=True).stdout.strip()
            self.assertTrue(not status or status.startswith("Z"), "Child still running: " + status)

    def test_runner_cancellation_closes_worker_input(self):
        with tempfile.TemporaryDirectory() as folder:
            log = Path(folder) / "supervisor.log"
            stopped = threading.Event()
            timer = threading.Timer(0.5, stopped.set)
            timer.start()
            # A synthetic SSH command which exits after the supervisor closes stdin.
            command = [sys.executable, "-c", "import sys; sys.stdin.read(); print('EOF cleanup complete')"]
            try:
                with log.open("wb") as stream, self.assertRaisesRegex(RuntimeError, "cancelled"):
                    supervise(command, stream, stopped)
            finally:
                timer.cancel()
            self.assertIn("EOF cleanup complete", log.read_text())


if __name__ == "__main__":
    unittest.main(verbosity=2)
