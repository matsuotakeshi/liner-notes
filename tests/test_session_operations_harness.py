"""Failure propagation and restart ordering for the real-store harness."""
import contextlib
import io
import subprocess
import unittest
from unittest.mock import patch

from test_session_operations import run_checks


class HarnessTests(unittest.TestCase):
    def setUp(self):
        self.output = io.StringIO()
        self.enterContext(contextlib.redirect_stdout(self.output))
        self.enterContext(contextlib.redirect_stderr(self.output))

    @staticmethod
    def result(output='', code=0):
        return subprocess.CompletedProcess([], code, output, '')

    def test_fresh_process_receives_original_ids_and_store(self):
        with patch('test_session_operations.subprocess.run', side_effect=[
            self.result('SESSION_FIXTURE 123 456\n'), self.result()
        ]) as run:
            run_checks('adapter', 'isolated store')
        self.assertEqual([call.args[0] for call in run.call_args_list], [
            ['adapter', '--session-seed', 'isolated store'],
            ['adapter', '--session-verify', 'isolated store', '123', '456'],
        ])

    def test_failed_seed_does_not_start_verifier(self):
        with patch('test_session_operations.subprocess.run', return_value=
                   self.result('SESSION_FIXTURE 1 2\n', 1)) as run:
            with self.assertRaisesRegex(RuntimeError, 'phase=seed exit=1'):
                run_checks('adapter', 'store')
            self.assertEqual(run.call_count, 1)

    def test_bad_identity_manifest_does_not_start_verifier(self):
        for manifest in ('', 'SESSION_FIXTURE 0 1\n', 'SESSION_FIXTURE 4294967296 1\n',
                         'SESSION_FIXTURE 1 2\nSESSION_FIXTURE 3 4\n'):
            with self.subTest(manifest=manifest), patch(
                'test_session_operations.subprocess.run', return_value=self.result(manifest)
            ) as run:
                with self.assertRaisesRegex(RuntimeError, 'identity pair'):
                    run_checks('adapter', 'store')
                self.assertEqual(run.call_count, 1)

    def test_failed_verifier_is_not_success(self):
        with patch('test_session_operations.subprocess.run', side_effect=[
            self.result('SESSION_FIXTURE 1 2\n'), self.result(code=-6)
        ]):
            with self.assertRaisesRegex(RuntimeError, 'phase=verify exit=-6'):
                run_checks('adapter', 'store')

    def test_timeout_preserves_last_operation_and_stops(self):
        with patch('test_session_operations.subprocess.run', side_effect=
                   subprocess.TimeoutExpired('adapter', 120, stderr=b'SESSION-API seed stop\n')) as run:
            with self.assertRaises(subprocess.TimeoutExpired):
                run_checks('adapter', 'store')
            self.assertEqual(run.call_count, 1)
        self.assertIn('timeout phase=seed', self.output.getvalue())
        self.assertIn('SESSION-API seed stop', self.output.getvalue())


if __name__ == '__main__':
    unittest.main()
