"""Hosted POSIX fixture: shared native/package/setup lock, never SSH/root."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'ci'))
from native_remote import host_lock
from freebsd_package_lease import runner


class Admission(unittest.TestCase):
    @unittest.skipUnless(os.name == 'posix', 'Remote POSIX shell fixture runs on hosted Linux')
    def test_workflow_preparation_uses_explicit_sh_and_stops_on_failure(self):
        import textwrap
        workflow = (ROOT/'.github/workflows/freebsd-package.yml').read_text()
        marker = "takeshi@10.0.0.50 /bin/sh -s <<'REMOTE'\n"
        self.assertEqual(workflow.count(marker), 1)
        script = textwrap.dedent(workflow.split(marker)[1].split('          REMOTE\n')[0])
        with tempfile.TemporaryDirectory() as temporary:
            home = Path(temporary)/'home'
            home.mkdir()
            env = dict(os.environ, HOME=str(home), SHELL='/bin/csh')
            result = subprocess.run(['/bin/sh', '-s'], input=script, text=True, env=env, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            created = Path(result.stdout.strip())
            self.assertEqual(created.parent, home/'ci')
            self.assertTrue(created.name.startswith('freebsd-pkg-'))
            self.assertEqual(created.stat().st_mode & 0o777, 0o700)
            blocked = Path(temporary)/'not-a-directory'
            blocked.write_text('unchanged')
            result = subprocess.run(['/bin/sh', '-s'], input=script, text=True,
                                    env=dict(env, HOME=str(blocked)), capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, '')
            self.assertEqual(blocked.read_text(), 'unchanged')

    def test_pr_never_reaches_ssh_setup(self):
        sha = 'a'*40
        env = dict(GITHUB_SHA=sha, GITHUB_REF='refs/heads/feature', GITHUB_EVENT_NAME='pull_request',
                   GITHUB_REPOSITORY='matsuotakeshi/liner-notes')
        with patch.dict(os.environ, env), patch('subprocess.check_output', return_value=sha), \
                patch('subprocess.Popen', side_effect=AssertionError('SSH attempted')):
            with self.assertRaises(ValueError):
                runner('/home/takeshi/ci/freebsd-pkg-fixture', sha, 'b'*64)


@unittest.skipUnless(os.name == 'posix', 'Actual POSIX flock/process-group behavior runs on hosted Linux')
class Lease(unittest.TestCase):
    def launch(self, lock, commands, timeout=8, lease=2):
        code = ('import sys,json,signal,threading;sys.path.insert(0,sys.argv[1]);'
                'from freebsd_package_lease import locked_commands;'
                's=threading.Event();'
                '[signal.signal(n,lambda n,f:s.set()) for n in (signal.SIGTERM,signal.SIGINT,signal.SIGHUP)];'
                'locked_commands(sys.argv[2],json.loads(sys.argv[3]),0,float(sys.argv[4]),float(sys.argv[5]),s.is_set)')
        process = subprocess.Popen([sys.executable, '-u', '-c', code, str(ROOT/'ci'), str(lock), json.dumps(commands), str(timeout), str(lease)],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        def cleanup():
            try:
                process.stdin.close()
            except BrokenPipeError:
                pass
            if process.poll() is None:
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            process.stdout.close()
        self.addCleanup(cleanup)
        process.stdin.write(b'alive\n')
        process.stdin.flush()
        return process

    def finish(self, process, success):
        try:
            process.wait(timeout=12)
            output = process.stdout.read().decode()
            self.assertEqual(process.returncode == 0, success, output)
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

    def marker_command(self, marker, sleep=0):
        return [[sys.executable, '-c', 'import os,time,sys;from pathlib import Path;Path(sys.argv[1]).write_text(str(os.getpid()));time.sleep(float(sys.argv[2]))', str(marker), str(sleep)]]

    def test_native_package_serialization(self):
        with tempfile.TemporaryDirectory() as temporary:
            lock, marker = Path(temporary)/'shared.lock', Path(temporary)/'started'
            with host_lock(lock):
                process = self.launch(lock, self.marker_command(marker))
                time.sleep(.3)
                self.assertFalse(marker.exists())
            self.finish(process, True)
            self.assertTrue(marker.exists())
            with host_lock(lock):
                pass

    def test_readonly_setup_lock_blocks_native_and_package_without_writes(self):
        import fcntl
        with tempfile.TemporaryDirectory() as temporary:
            lock, marker = Path(temporary)/'shared.lock', Path(temporary)/'started'
            lock.write_bytes(b'unchanged')
            before = lock.stat().st_mtime_ns
            fd = os.open(lock, os.O_RDONLY | os.O_NOFOLLOW)
            try:
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                with self.assertRaises(BlockingIOError), host_lock(lock):
                    self.fail('native overlapped setup')
                process = self.launch(lock, self.marker_command(marker))
                time.sleep(.3)
                self.assertFalse(marker.exists())
            finally:
                os.close(fd)
            self.finish(process, True)
            self.assertEqual(lock.read_bytes(), b'unchanged')
            self.assertEqual(lock.stat().st_mtime_ns, before)

    def test_waiting_disconnect_and_signal_never_start_package(self):
        for mode in ('disconnect', 'signal'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temporary:
                lock, marker = Path(temporary)/'shared.lock', Path(temporary)/'started'
                with host_lock(lock):
                    process = self.launch(lock, self.marker_command(marker))
                    time.sleep(.2)
                    if mode == 'disconnect':
                        process.stdin.close()
                    else:
                        process.send_signal(signal.SIGTERM)
                    self.finish(process, False)
                    self.assertFalse(marker.exists())
                with host_lock(lock):
                    pass

    def test_running_disconnect_timeout_and_expired_lease_reap_children(self):
        for mode in ('disconnect', 'timeout', 'lease'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as temporary:
                lock, marker = Path(temporary)/'shared.lock', Path(temporary)/'started'
                process = self.launch(lock, self.marker_command(marker, 30), timeout=.7 if mode == 'timeout' else 8,
                                      lease=.7 if mode == 'lease' else 3)
                deadline = time.monotonic()+5
                while not marker.exists() and time.monotonic() < deadline:
                    time.sleep(.02)
                self.assertTrue(marker.exists())
                pid = int(marker.read_text())
                if mode == 'disconnect':
                    process.stdin.close()
                self.finish(process, False)
                status = subprocess.run(['ps', '-o', 'stat=', '-p', str(pid)], capture_output=True, text=True).stdout.strip()
                self.assertTrue(not status or status.startswith('Z'), 'Package process still running')
                with host_lock(lock):
                    pass

    def test_failed_package_command_releases_native_lock(self):
        with tempfile.TemporaryDirectory() as temporary:
            lock = Path(temporary)/'shared.lock'
            process = self.launch(lock, [[sys.executable, '-c', 'raise SystemExit(7)']])
            self.assertIn('command failed', self.finish(process, False))
            with host_lock(lock):
                pass


if __name__ == '__main__':
    unittest.main()
