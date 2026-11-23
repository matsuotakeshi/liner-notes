"""Validate the candidate policy and Unix listener with real worker processes."""

import json
import os
from pathlib import Path
import select
import signal
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / name) for name in ("solaris", "scripts")]
if os.name == "posix":
    import door_solaris_supervisor as supervisor


def worker_fixture(path):
    def observe(policy, connection, watch):
        connection.sendall(b"open\n")
        readable, _, _ = select.select([watch], [], [], 8)
        if readable and os.read(watch, 1) == b"":
            connection.sendall(b"stopped\n")

    supervisor.serve_launch = observe
    supervisor.run_supervisor(dict(socket=path, socket_gid=os.getgid(),
                                   bbs_uid=os.geteuid(), slots=[{}]))


@unittest.skipUnless(os.name == "posix", "POSIX process and Unix socket")
class SolarisSupervisor(unittest.TestCase):
    def test_policy_cross_checks_broker_and_root_paths(self):
        with tempfile.TemporaryDirectory(prefix="ln-policy-") as scratch:
            area = Path(scratch)
            state = area / "state"
            state.mkdir(mode=0o700)
            helper = area / "helper"
            helper.touch(mode=0o700)
            endpoint = area / "socket"
            document = area / "isolation.json"
            broker = dict(bbs_uid=61002, broker_uid=61003, socket=str(area / "broker"),
                          door_uids=[61001, 61005], consumers={61004: [1]})
            slots = [dict(door=1, uid=uid, level=0, seconds=30,
                          root=str(area / ("root-" + str(uid))), helper=str(helper))
                     for uid in broker["door_uids"]]
            policy = dict(bbs_uid=61002, socket=str(endpoint), socket_gid=61002,
                          broker_config=str(area / "broker.json"), state_dir=str(state), slots=slots)
            with (patch.object(supervisor, "secure_path", side_effect=lambda path, *_,
                               **options: Path(path)),
                  patch.object(supervisor, "broker_configuration", return_value=broker)):
                document.write_text(json.dumps(policy))
                self.assertEqual(supervisor.read_policy(document)["broker_uid"], 61003)
                for invalid in (
                    dict(policy, bbs_uid=61006),
                    dict(policy, socket=broker["socket"]),
                    dict(policy, slots=[slots[0], dict(slots[1], uid=slots[0]["uid"])]),
                    dict(policy, slots=[slots[0], dict(slots[1], root=slots[0]["root"] + "/under")]),
                    dict(policy, slots=[slots[0], dict(slots[1], helper=slots[0]["root"] + "/door")]),
                    dict(policy, slots=[dict(slots[0], door=2), slots[1]]),
                ):
                    document.write_text(json.dumps(invalid))
                    with self.subTest(policy=invalid), self.assertRaises(ValueError):
                        supervisor.read_policy(document)

    def test_listener_stops_and_waits_for_real_worker(self):
        with tempfile.TemporaryDirectory(prefix="ln-sup-", dir="/tmp") as scratch:
            endpoint = Path(scratch) / "door.sock"
            with subprocess.Popen([sys.executable, __file__, "--fixture", str(endpoint)],
                                  stderr=subprocess.PIPE) as parent:
                try:
                    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                        import time
                        until = time.monotonic() + 5
                        while True:
                            try:
                                client.connect(str(endpoint))
                                break
                            except (FileNotFoundError, ConnectionRefusedError):
                                if time.monotonic() >= until or parent.poll() is not None:
                                    self.fail("Supervisor listener did not start")
                                time.sleep(0.02)
                        client.settimeout(5)
                        with client.makefile("rb") as stream:
                            self.assertEqual(stream.readline(), b"open\n")
                            parent.send_signal(signal.SIGTERM)
                            self.assertEqual(stream.readline(), b"stopped\n")
                    self.assertEqual(parent.wait(timeout=5), 0)
                    self.assertFalse(endpoint.exists())
                finally:
                    if parent.poll() is None:
                        parent.kill()


if __name__ == "__main__":
    if sys.argv[1:2] == ["--fixture"]:
        worker_fixture(sys.argv[2])
    else:
        unittest.main()
