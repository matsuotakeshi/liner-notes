"""Server harness failures retain evidence without weakening acceptance gates."""
import io
import os
from pathlib import Path
import subprocess
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import test_board
import test_members
from test_runtime import test_wait_timeout


class ImmediateThread:
    def __init__(self, target, daemon):
        self.target = target

    def start(self):
        self.target()

    def join(self, timeout):
        self.timeout = timeout


class WaitingThread(ImmediateThread):
    def start(self):
        pass  # synthetic live child that has not produced a startup line


def process(stdout='', stderr='', code=None):
    child = Mock(pid=1234, returncode=code)
    child.stdout = io.StringIO(stdout)
    child.stderr = io.StringIO(stderr)
    child.poll.return_value = code

    def kill():
        child.returncode = -9

    child.kill.side_effect = kill
    child.wait.side_effect = lambda timeout=None: child.returncode
    return child


class DiagnosticsTests(unittest.TestCase):
    def setUp(self):
        env = patch.dict(os.environ, {}, clear=True)
        env.start()
        self.addCleanup(env.stop)

    def test_timeout_override_is_bounded(self):
        self.assertEqual(test_wait_timeout('LINER_TEST_AUTH_TIMEOUT', 10), 10)
        for value in ('30', '120', '0.5'):
            with patch.dict(os.environ, {'LINER_TEST_AUTH_TIMEOUT': value}):
                self.assertEqual(test_wait_timeout('LINER_TEST_AUTH_TIMEOUT', 10), float(value))
        for value in ('0', '-1', '121', 'inf', 'nan', 'invalid'):
            with patch.dict(os.environ, {'LINER_TEST_AUTH_TIMEOUT': value}), self.assertRaises(ValueError):
                test_wait_timeout('LINER_TEST_AUTH_TIMEOUT', 10)

    def test_auth_uses_override_and_still_requires_login_response(self):
        for value, expected in ((None, 10), ('30', 30)):
            client = Mock()
            client.line.return_value = b'PASSWORD'
            env = {} if value is None else {'LINER_TEST_AUTH_TIMEOUT': value}
            with patch.dict(os.environ, env):
                test_members.auth(client, 'bob', True)
                client.sock.settimeout.assert_called_once_with(expected)
                client.expect.assert_called_once_with(b'LOGGED IN bob')
                client.expect.side_effect = TimeoutError('still stalled')
                with self.assertRaises(TimeoutError):
                    test_members.auth(client, 'bob', True)

    def test_extended_shutdown_still_fails_and_keeps_diagnostics(self):
        with patch.dict(os.environ, {'LINER_TEST_STOP_TIMEOUT': '30'}):
            evidence = self.shutdown_failure(SimpleNamespace(stdout=' D 344064 00:35'), wait=30)
        self.assertEqual(evidence['ps'], 'D 344064 00:35')
        self.assertEqual(evidence['exit_after_kill'], -9)

    def test_extended_shutdown_can_finish_without_kill(self):
        server = object.__new__(test_board.Server)
        server.proc, server.expected_errors = process(code=0), []
        with patch.dict(os.environ, {'LINER_TEST_STOP_TIMEOUT': '30'}):
            server.close()
        server.proc.wait.assert_called_once_with(timeout=30)
        server.proc.kill.assert_not_called()

    def start_failure(self, child, thread=ImmediateThread):
        with patch.object(test_board.subprocess, 'Popen', return_value=child), \
             patch.object(test_board.threading, 'Thread', thread), \
             patch.dict(os.environ, {'LINER_TEST_START_TIMEOUT': '5'}), \
             self.assertRaises(AssertionError) as failure:
            test_board.Server('unused-runtime', Path('unused-store'))
        message = failure.exception.args[0]
        self.assertEqual(message[0], 'startup')
        return message[1], dict(zip(message[2::2], message[3::2]))

    def test_exit_status_and_stderr_survive_cleanup(self):
        child = process(stderr='board: corrupt synthetic store\n', code=23)
        lines, evidence = self.start_failure(child)
        self.assertEqual(lines, [''])
        self.assertEqual(evidence, {'exit_before_cleanup': 23,
                                   'exit_after_cleanup': 23,
                                   'stderr': 'board: corrupt synthetic store\n'})
        child.kill.assert_not_called()
        child.wait.assert_called_once_with()

    def test_posix_signal_status_is_not_replaced_by_cleanup_kill(self):
        child = process(code=-11)
        _, evidence = self.start_failure(child)
        self.assertEqual(evidence['exit_before_cleanup'], -11)
        self.assertEqual(evidence['exit_after_cleanup'], -11)
        child.kill.assert_not_called()

    def test_clean_exit_without_listener_still_fails(self):
        child = process(code=0)
        _, evidence = self.start_failure(child)
        self.assertEqual(evidence['exit_before_cleanup'], 0)
        child.kill.assert_not_called()

    def test_eof_does_not_claim_child_already_exited(self):
        child = process()
        lines, evidence = self.start_failure(child)
        self.assertEqual(lines, [''])
        self.assertIsNone(evidence['exit_before_cleanup'])
        self.assertEqual(evidence['exit_after_cleanup'], -9)
        child.kill.assert_called_once_with()

    def test_no_startup_line_preserves_deadline_and_running_state(self):
        child = process()
        with patch.object(WaitingThread, 'join', autospec=True) as joined:
            lines, evidence = self.start_failure(child, WaitingThread)
        self.assertEqual(joined.call_args.args[1], 5)
        self.assertEqual(lines, [])
        self.assertIsNone(evidence['exit_before_cleanup'])
        self.assertEqual(evidence['exit_after_cleanup'], -9)
        child.kill.assert_called_once_with()

    def test_malformed_startup_line_is_not_accepted(self):
        child = process(stdout='not a listener\n')
        lines, _ = self.start_failure(child)
        self.assertEqual(lines, ['not a listener\n'])
        child.kill.assert_called_once_with()

    def shutdown_failure(self, ps_result=None, ps_error=None, wait=5):
        child = process(stderr='synthetic shutdown failure\n')
        timeout = subprocess.TimeoutExpired('unused-runtime', wait)
        child.wait.side_effect = [timeout, -9]
        server = object.__new__(test_board.Server)
        server.proc, server.expected_errors = child, []
        with patch.object(test_board.subprocess, 'run', return_value=ps_result,
                          side_effect=ps_error) as snapshot, \
             self.assertRaises(AssertionError) as failure:
            server.close()
        self.assertIs(failure.exception.__cause__, timeout)
        self.assertEqual(child.wait.call_args_list[0].kwargs, {'timeout': wait})
        child.terminate.assert_called_once_with()
        child.kill.assert_called_once_with()
        snapshot.assert_called_once_with(
            ['ps', '-o', 'stat=,rss=,etime=', '-p', '1234'],
            capture_output=True, text=True, timeout=2, check=False)
        message = failure.exception.args[0]
        self.assertEqual(message[0], f'shutdown exceeded {wait} seconds after terminate')
        return dict(zip(message[1::2], message[2::2]))

    def test_shutdown_timeout_remains_failure_with_process_snapshot(self):
        evidence = self.shutdown_failure(SimpleNamespace(stdout=' S 4096 00:05\n'))
        self.assertEqual(evidence, {'ps': 'S 4096 00:05', 'exit_after_kill': -9,
                                   'stderr': 'synthetic shutdown failure\n'})

    def test_missing_ps_does_not_hide_shutdown_failure(self):
        evidence = self.shutdown_failure(ps_error=FileNotFoundError('ps'))
        self.assertEqual(evidence['ps'], 'unavailable')
        self.assertEqual(evidence['exit_after_kill'], -9)

    def test_hanging_ps_does_not_hide_shutdown_failure(self):
        evidence = self.shutdown_failure(ps_error=subprocess.TimeoutExpired('ps', 2))
        self.assertEqual(evidence['ps'], 'unavailable')

    def test_explicit_stderr_validator_runs_after_process_wait(self):
        server = object.__new__(test_board.Server)
        server.proc = process(stderr='expected audit\n', code=0)
        server.expected_errors = []
        def validate(errors):
            server.proc.terminate.assert_called_once_with()
            server.proc.wait.assert_called_once_with(timeout=5)
            self.assertEqual(errors, 'expected audit\n')
        check = Mock(side_effect=validate)
        server.close(error_check=check)
        check.assert_called_once_with('expected audit\n')
        server.proc.kill.assert_not_called()

    def test_stderr_validator_failure_is_not_swallowed(self):
        server = object.__new__(test_board.Server)
        server.proc = process(stderr='wrong audit\n', code=0)
        server.expected_errors = []
        check = Mock(side_effect=AssertionError('audit rejected'))
        with self.assertRaisesRegex(AssertionError, 'audit rejected'):
            server.close(error_check=check)
        server.proc.wait.assert_called_once_with(timeout=5)
        check.assert_called_once_with('wrong audit\n')

    def test_normal_shutdown_keeps_stderr_assertion(self):
        server = object.__new__(test_board.Server)
        server.proc = process(stderr='unexpected stderr\n', code=0)
        server.expected_errors = []
        with self.assertRaisesRegex(AssertionError, 'unexpected stderr'):
            server.close()
        server.proc.wait.assert_called_once_with(timeout=5)
        server.proc.kill.assert_not_called()


if __name__ == '__main__':
    unittest.main()
