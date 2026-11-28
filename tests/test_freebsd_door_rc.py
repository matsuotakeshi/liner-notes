"""Run both BBS rc templates with a fake daemon and a real test Unix socket."""
import os
from pathlib import Path
import shlex
import socket
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ('rc.d/liner_notes', 'ports/comms/liner-notes/files/liner_notes.in')


@unittest.skipUnless(os.name == 'posix', 'requires a POSIX rc shell and Unix socket')
class DoorRcTests(unittest.TestCase):
    def setUp(self):
        fixture = tempfile.TemporaryDirectory(prefix='ln-rc-')
        self.addCleanup(fixture.cleanup)
        self.directory = Path(fixture.name)
        self.daemon = self.directory/'daemon'
        self.daemon.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$CAPTURE"\n')
        self.daemon.chmod(0o700)
        self.conf = self.directory/'rc.conf'
        self.conf.touch()

    def start(self, path, value):
        # No service manager or processes outside the fixture are used. Preserve
        # the actual socket preflight and launch argument construction.
        text = (ROOT/path).read_text()
        text = text.replace('. /etc/rc.subr', 'check_pidfile() { return 1; }')
        text = text.replace('exec /usr/sbin/daemon ', 'exec '+shlex.quote(str(self.daemon))+' ')
        text = text.replace('run_rc_command "$1"', '''
liner_notes_validate() { liner_notes_validate_isolation; }
liner_notes_ready() { return 0; }
liner_notes_start
''')
        capture = self.directory/'args'
        capture.unlink(missing_ok=True)
        env = dict(os.environ, LINER_NOTES_RC_CONF=str(self.conf), CAPTURE=str(capture),
                   liner_notes_home=str(self.directory), liner_notes_door_isolation_socket=value)
        result = subprocess.run(['/bin/sh', '-s'], input=text, env=env,
                                capture_output=True, text=True, timeout=5)
        return result, capture.read_text().splitlines() if capture.exists() else []

    def test_empty_keeps_legacy_launch(self):
        for script in SCRIPTS:
            with self.subTest(script=script):
                result, argv = self.start(script, '')
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn('--door-isolation-socket', argv)

    def test_configured_socket_is_a_single_argument(self):
        endpoint = self.directory/'control socket.sock'
        with socket.socket(socket.AF_UNIX) as listener:
            listener.bind(str(endpoint))
            for script in SCRIPTS:
                with self.subTest(script=script):
                    result, argv = self.start(script, str(endpoint))
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(argv.count('--door-isolation-socket'), 1)
                    self.assertEqual(argv[argv.index('--door-isolation-socket')+1], str(endpoint))

    def test_invalid_setting_is_rejected_before_launch(self):
        regular = self.directory/'regular'
        regular.touch()
        for script in SCRIPTS:
            for value, reason in (('relative.sock', 'absolute path'),
                                  ('/'+'x'*99, '99 bytes'),
                                  (str(regular), 'missing or invalid'),
                                  (str(self.directory/'missing'), 'missing or invalid')):
                with self.subTest(script=script, value=value):
                    result, argv = self.start(script, value)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(reason, result.stderr)
                    self.assertFalse(argv)


if __name__ == '__main__':
    unittest.main()
