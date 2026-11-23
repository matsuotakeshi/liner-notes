"""Test the Solaris supervisor boundary without launching a privileged Door."""

from contextlib import ExitStack, contextmanager
import os
from pathlib import Path
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import Mock, call, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "solaris"), str(ROOT / "scripts")]
import door_solaris_lifecycle as lifecycle


class PreparedStatus(unittest.TestCase):
    def check_status(self, pieces, holder_status=None):
        with (
            patch.object(lifecycle.select, "select", return_value=([9], [], [])),
            patch.object(lifecycle.os, "read", side_effect=pieces),
        ):
            lifecycle._await_prepared(9, Mock(poll=Mock(return_value=holder_status)))

    def test_fragmented_prepared_status(self):
        self.check_status([b"PRE", b"PARED\n"])

    def test_empty_unknown_and_failed_exec_status_refuse(self):
        for pieces in ([b""], [b"ERR credentials\n"], [b"PREPARED\nERR exec\n"]):
            with self.subTest(pieces=pieces), self.assertRaises(OSError):
                self.check_status(pieces)
        with self.assertRaisesRegex(OSError, "contract already ended"):
            self.check_status([b"PREPARED\n"], holder_status=126)

    def test_timeout_does_not_read_or_accept_a_status(self):
        with (
            patch.object(lifecycle.select, "select", return_value=([], [], [])),
            patch.object(lifecycle.os, "read") as read,
            self.assertRaises(TimeoutError),
        ):
            lifecycle._await_prepared(9, Mock())
        read.assert_not_called()


@unittest.skipUnless(os.name == "posix", "Unix FIFO and terminal semantics")
class OwnedLifecycle(unittest.TestCase):
    def test_fifo_is_directional_and_unlinked_while_in_use(self):
        import fcntl

        with TemporaryDirectory() as directory:
            reader, writer = lifecycle._directional_channel(directory)
            try:
                self.assertEqual(list(Path(directory).iterdir()), [])
                self.assertEqual(fcntl.fcntl(reader, fcntl.F_GETFL) & os.O_ACCMODE, os.O_RDONLY)
                self.assertEqual(fcntl.fcntl(writer, fcntl.F_GETFL) & os.O_ACCMODE, os.O_WRONLY)
                self.assertFalse(os.get_inheritable(reader) or os.get_inheritable(writer))
                self.assertEqual(os.write(writer, b"fixture"), 7)
                self.assertEqual(os.read(reader, 7), b"fixture")
            finally:
                os.close(writer)
                os.close(reader)

    @contextmanager
    def fixture(self, *, boundary_error=None, close_error=None, ticket="a"*64):
        log = Mock()
        holder = log.spawn.return_value
        holder.poll.return_value = None
        log.issue.return_value = (log.issuer, ticket)
        log.issuer.close.side_effect = close_error
        log.await_prepared.side_effect = boundary_error

        @contextmanager
        def admitted(*_):
            yield 11, log.slot

        def clear_after_stop(stop, *, settle):
            self.assertEqual(settle, 2)
            stop()
            log.released()

        log.slot.stop_and_release.side_effect = clear_after_stop
        with ExitStack() as patches:
            patches.enter_context(patch.object(lifecycle, "secure_path", return_value=Path("/helper")))
            patches.enter_context(patch.object(lifecycle, "admit", admitted))
            patches.enter_context(patch.object(lifecycle, "_directional_channel", side_effect=[(16,17),(18,19)]))
            patches.enter_context(patch.object(lifecycle, "_await_prepared", log.await_prepared))
            patches.enter_context(patch.object(lifecycle.subprocess, "Popen", log.spawn))
            patches.enter_context(patch.object(lifecycle.os, "write", return_value=65))
            patches.enter_context(patch.object(lifecycle.os, "close"))
            patches.enter_context(patch("pty.openpty", return_value=(14,15)))
            patches.enter_context(patch("fcntl.ioctl"))
            yield log

    def launch(self, log):
        return lifecycle.launch_prepared("/root", "/state", 61001, "/helper", 80, 24, log.issue)

    def test_revocation_then_owned_stop_then_release(self):
        with self.fixture() as log:
            with self.launch(log) as prepared:
                self.assertEqual((prepared.master, prepared.status), (14, 18))
                self.assertIs(prepared.issuer, log.issuer)
                log.issuer.close.assert_not_called()
            args, options = log.spawn.call_args
            self.assertEqual(args[0][:5], ("/usr/bin/ctrun", "-l", "contract", "-o", "noorphan"))
            self.assertEqual(options["pass_fds"], (11,15,16,19))
            self.assertNotIn("a"*64, repr(log.spawn.call_args))
            self.assertLess(log.mock_calls.index(call.issuer.close()),
                            log.mock_calls.index(call.spawn().kill()))
            self.assertLess(log.mock_calls.index(call.spawn().wait(timeout=5)),
                            log.mock_calls.index(call.released()))

    def test_failed_prepare_stops_before_returning_the_pty(self):
        with self.fixture(boundary_error=TimeoutError) as log:
            with self.assertRaises(TimeoutError), self.launch(log):
                self.fail("PTY was exposed after a boundary startup timeout")
            log.issuer.close.assert_called_once()
            log.spawn.return_value.kill.assert_called_once()
            log.released.assert_called_once()

    def test_failed_revocation_stops_without_clearing_busy(self):
        with self.fixture(close_error=OSError("unknown Broker state")) as log:
            with self.assertRaisesRegex(OSError, "unknown Broker state"), self.launch(log):
                pass
            log.spawn.return_value.kill.assert_called_once()
            log.slot.stop_and_release.assert_not_called()

    def test_invalid_broker_reply_closes_issuer_and_never_starts(self):
        with self.fixture(ticket="bad ticket") as log:
            with self.assertRaisesRegex(ValueError, "Broker reply"), self.launch(log):
                self.fail("Invalid ticket was accepted")
            log.issuer.close.assert_called_once()
            log.spawn.assert_not_called()
            log.released.assert_called_once()


if __name__ == "__main__":
    unittest.main()
