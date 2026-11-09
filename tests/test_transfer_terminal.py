"""Exact terminal restoration after a configured PTY enters raw mode."""
import os
from pathlib import Path
import sys
import unittest
from transfer_terminal_fixture import terminal_baseline, assert_timeout_cleanup

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from zmodem_connector import raw_terminal
if os.name == 'posix':
    import pty
    import termios


@unittest.skipUnless(os.name == 'posix', 'POSIX terminal required')
class TerminalTests(unittest.TestCase):
    def setUp(self):
        self.master, self.slave = pty.openpty()
        self.before = terminal_baseline(self.slave)

    def tearDown(self):
        os.close(self.master); os.close(self.slave)

    def test_raw_normal_and_exception_restore_all_attributes(self):
        with raw_terminal(self.slave):
            self.assertFalse(termios.tcgetattr(self.slave)[3] & termios.ICANON)
        assert_timeout_cleanup(self.slave, self.before, 1)
        with self.assertRaisesRegex(RuntimeError, 'fixture failure'):
            with raw_terminal(self.slave):
                raise RuntimeError('fixture failure')
        assert_timeout_cleanup(self.slave, self.before, 1)

    def test_four_second_boundary_is_still_rejected(self):
        with self.assertRaises(AssertionError):
            assert_timeout_cleanup(self.slave, self.before, 4)

    def test_changed_local_flag_is_rejected(self):
        changed = termios.tcgetattr(self.slave); changed[3] ^= termios.ECHO
        termios.tcsetattr(self.slave, termios.TCSANOW, changed)
        with self.assertRaises(AssertionError):
            assert_timeout_cleanup(self.slave, self.before, 1)

    def test_changed_control_character_is_rejected(self):
        changed = termios.tcgetattr(self.slave)
        previous = changed[6][termios.VEOF]
        changed[6][termios.VEOF] = b'\x05' if previous != b'\x05' else b'\x04'
        termios.tcsetattr(self.slave, termios.TCSANOW, changed)
        with self.assertRaises(AssertionError):
            assert_timeout_cleanup(self.slave, self.before, 1)

    def test_changed_speed_is_rejected(self):
        changed = termios.tcgetattr(self.slave)
        speed = termios.B4800 if changed[5] != termios.B4800 else termios.B9600
        changed[4] = changed[5] = speed
        termios.tcsetattr(self.slave, termios.TCSANOW, changed)
        with self.assertRaises(AssertionError):
            assert_timeout_cleanup(self.slave, self.before, 1)


if __name__ == '__main__':
    unittest.main()
