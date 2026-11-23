"""Test the authenticated handoff without enabling or starting a service."""

import array
from contextlib import ExitStack, contextmanager, nullcontext
import os
from pathlib import Path
import select
import socket
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, call, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "solaris"), str(ROOT / "scripts")]
if os.name == "posix":
    import door_solaris_service as service


@unittest.skipUnless(os.name == "posix", "Unix socket and PTY handoff")
class SolarisService(unittest.TestCase):
    def test_busy_slot_can_be_skipped_before_launch(self):
        slots = [dict(door=3, level=4, uid=1, root="/restricted"),
                 dict(door=3, level=2, uid=2, root="/busy"),
                 dict(door=3, level=2, uid=3, root="/available")]
        config = dict(slots=slots, state_dir="/state")
        request = dict(door=3, level=2)
        with (
            ExitStack() as stack,
            patch.object(service, "admit", side_effect=[
                service.SlotBusy("already in use"), nullcontext((5, "lease")),
            ]) as admit,
        ):
            self.assertEqual(service.select_slot(config, request, stack), (slots[2], 5, "lease"))
            self.assertEqual(admit.call_args_list, [
                call("/busy", "/state", 2), call("/available", "/state", 3),
            ])

    def test_consumer_poll_requires_a_boolean_success_on_its_issuer(self):
        issuer = Mock()
        watch = Mock(door=SimpleNamespace(issuer=issuer))
        watch.ended.return_value = False
        with (
            patch.object(service.time, "monotonic", return_value=2),
            patch.object(service, "send") as send,
            patch.object(service, "read_line", side_effect=[
                {"ok": True, "result": {"consumed": False}},
                {"ok": True, "result": {"consumed": True}},
            ]),
        ):
            service.wait_for_consumer(watch, 5)
        self.assertEqual(send.call_args_list, [call(issuer, {"op": "ready"})] * 2)
        for reply in ({"ok": False}, {"ok": True, "result": {"consumed": 1}}):
            with (
                self.subTest(reply=reply),
                patch.object(service.time, "monotonic", return_value=2),
                patch.object(service, "send"),
                patch.object(service, "read_line", return_value=reply),
                self.assertRaises(OSError),
            ):
                service.wait_for_consumer(watch, 5)

    def test_status_eof_is_not_handoff_and_client_close_ends_watch(self):
        server, client = socket.socketpair()
        issuer, consumer = socket.socketpair()
        with server, client, issuer, consumer:
            read_fd, write_fd = os.pipe()
            door = SimpleNamespace(contract=Mock(poll=Mock(return_value=None)),
                                   issuer=issuer, status=read_fd)
            try:
                os.close(write_fd)
                watch = service.DoorWatch(door, server, None)
                self.assertFalse(watch.ended(0))
                self.assertIsNone(watch.status)
                client.close()
                self.assertTrue(watch.ended(1))
            finally:
                os.close(read_fd)

    def test_exec_error_after_prepared_ends_the_watch(self):
        door = SimpleNamespace(contract=Mock(poll=Mock(return_value=None)),
                               issuer=Mock(), status=39)
        with (
            patch.object(service.select, "select", return_value=([39], [], [])),
            patch.object(service.os, "read", return_value=b"ERR exec\n"),
            self.assertRaisesRegex(OSError, "failed after boundary"),
        ):
            service.DoorWatch(door, Mock(), None).ended(0)

    def test_only_the_trusted_bbs_and_a_ready_consumer_receive_the_pty(self):
        slot = dict(uid=61001, door=3, level=2, seconds=30, root="/root", helper="/helper")
        config = {"bbs_uid": 61002, "state_dir": "/state", "slots": [slot]}
        request = dict(door=3, level=2, user=7, columns=80, rows=24,
                       namehex="74657374", session="s", generation=0)

        for peer, ready in ((61002, True), (61002, False), (61004, True)):
            with self.subTest(peer=peer, ready=ready):
                log = Mock()
                connection = log.connection
                connection.sendmsg.return_value = 3
                log.watch.return_value.ended.return_value = True

                @contextmanager
                def launch(*args):
                    log.launched(args[:-1])
                    try:
                        yield SimpleNamespace(master=5)
                    finally:
                        log.revoked_and_stopped()

                def ready_check(*_):
                    connection.sendmsg.assert_not_called()
                    if not ready:
                        raise TimeoutError("test")

                with (
                    patch.object(service, "peer_uid", return_value=peer),
                    patch.object(service, "read_line", return_value=request) as read,
                    patch.object(service, "admit", return_value=nullcontext((4, "lease"))),
                    patch.object(service, "launch_reserved", launch),
                    patch.object(service, "DoorWatch", log.watch),
                    patch.object(service, "wait_for_consumer", side_effect=ready_check),
                    patch.object(service, "failure_event"),
                ):
                    self.assertEqual(service.serve_launch(config, connection),
                                     peer == 61002 and ready)
                if peer == 61002 and ready:
                    frames, rights = connection.sendmsg.call_args.args
                    self.assertEqual(frames, [b"OK\n"])
                    self.assertEqual(rights[0][:2], (socket.SOL_SOCKET, socket.SCM_RIGHTS))
                    self.assertEqual(list(rights[0][2]), [5])
                    self.assertLess(log.mock_calls.index(call.revoked_and_stopped()),
                                    log.mock_calls.index(call.connection.sendall(b"DONE\n")))
                else:
                    connection.sendmsg.assert_not_called()
                    connection.sendall.assert_called_once_with(b"ERR\n")
                if peer != 61002:
                    read.assert_not_called()
                    log.launched.assert_not_called()

    def test_handoff_passes_a_real_pty_on_a_unix_socket(self):
        import pty

        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)

        @contextmanager
        def launch(*_):
            worker_master = os.dup(master)
            try:
                yield SimpleNamespace(master=worker_master)
            finally:
                os.close(worker_master)

        slot = dict(uid=61001, door=3, level=2, seconds=30, root="/root", helper="/helper")
        config = {"bbs_uid": 61002, "state_dir": "/state", "slots": [slot]}
        request = dict(door=3, level=2, user=7, columns=80, rows=24,
                       namehex="74657374", session="s", generation=0)
        server, client = socket.socketpair()
        with (
            server, client,
            patch.object(service, "peer_uid", return_value=61002),
            patch.object(service, "read_line", return_value=request),
            patch.object(service, "admit", return_value=nullcontext((4, "lease"))),
            patch.object(service, "launch_reserved", launch),
            patch.object(service, "DoorWatch", return_value=Mock(ended=Mock(return_value=True))),
            patch.object(service, "wait_for_consumer"),
        ):
            client.settimeout(2)
            self.assertTrue(service.serve_launch(config, server))
            fds = array.array("i")
            data, rights, flags, _ = client.recvmsg(3, socket.CMSG_SPACE(fds.itemsize),
                                                    socket.MSG_WAITALL)
            for level, kind, payload in rights:
                if (level, kind) == (socket.SOL_SOCKET, socket.SCM_RIGHTS):
                    fds.frombytes(payload[:len(payload) - len(payload) % fds.itemsize])
            try:
                self.assertEqual(data, b"OK\n")
                self.assertFalse(flags & socket.MSG_CTRUNC)
                self.assertEqual(len(fds), 1)
                os.write(slave, b"pty-data")
                self.assertEqual(select.select([fds[0]], [], [], 2)[0], [fds[0]])
                self.assertEqual(os.read(fds[0], 64), b"pty-data")
                self.assertEqual(client.recv(5, socket.MSG_WAITALL), b"DONE\n")
            finally:
                for fd in fds:
                    os.close(fd)


if __name__ == "__main__":
    unittest.main()
