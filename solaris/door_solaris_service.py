"""One authenticated BBS handoff for the Solaris startup service candidate."""

import array
from contextlib import suppress
import os
import select
import socket
import time

from door_broker import peer_uid
from door_isolation import broker_open, failure_event, read_line, request_check, send
from door_solaris_lifecycle import launch_prepared


class DoorWatch:
    def __init__(self, door, connection, parent_watch):
        self.door = door
        self.peers = [connection, door.issuer]
        if parent_watch is not None:
            self.peers.append(parent_watch)
        self.status = door.status

    def ended(self, wait):
        watched = self.peers if self.status is None else [*self.peers, self.status]
        ready, _, _ = select.select(watched, [], [], wait)
        if self.door.contract.poll() is not None or any(peer in ready for peer in self.peers):
            return True
        if self.status in ready:
            if os.read(self.status, 32):
                raise OSError("Solaris Door failed after boundary preparation")
            self.status = None  # Exec may close the status writer. EOF alone is not readiness.
        return False


def wait_for_consumer(watch, deadline):
    issuer = watch.door.issuer
    while not watch.ended(0):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Solaris Door consumer readiness deadline")
        issuer.settimeout(min(3, remaining))
        send(issuer, {"op": "ready"})
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("Solaris Door consumer readiness deadline")
        reply = read_line(issuer, timeout=min(3, remaining))
        status = reply.get("result") if isinstance(reply, dict) and reply.get("ok") is True else None
        if not isinstance(status, dict) or type(status.get("consumed")) is not bool:
            raise OSError("Solaris Door Broker did not confirm readiness")
        if status["consumed"] and time.monotonic() < deadline and not watch.ended(0):
            return
        if watch.ended(min(0.05, max(0, deadline - time.monotonic()))):
            break
    raise OSError("Solaris Door closed before consumer readiness")


def serve_launch(config, slot, connection, parent_watch=None):
    """Relay one launch using the supervisor's already selected trusted slot."""
    handed_off = False
    phase = "request"
    try:
        if peer_uid(connection) != config["bbs_uid"]:
            raise ValueError("Solaris Door BBS identity mismatch")
        request = read_line(connection)
        request_check(request)
        if request["door"] != slot["door"] or request["level"] < slot["level"]:
            raise ValueError("Solaris Door slot policy")

        def issue():
            return broker_open(config, request, slot)

        deadline = time.monotonic() + 8
        phase = "launch"
        with launch_prepared(slot["root"], config["state_dir"], slot["uid"],
                             slot["helper"], request["columns"], request["rows"], issue) as door:
            watch = DoorWatch(door, connection, parent_watch)
            phase = "broker"
            wait_for_consumer(watch, deadline)
            phase = "handoff"
            connection.settimeout(3)
            rights = [(socket.SOL_SOCKET, socket.SCM_RIGHTS, array.array("i", [door.master]))]
            if connection.sendmsg([b"OK\n"], rights) != 3:
                raise OSError("Incomplete Solaris Door PTY handoff")
            handed_off = True
            phase = "running"
            end = time.monotonic() + slot["seconds"]
            while (remaining := end - time.monotonic()) > 0:
                if watch.ended(min(0.2, remaining)):
                    break
            phase = "cleanup"
        return True
    except (OSError, ValueError, KeyError, TypeError) as error:
        failure_event("launch-rejected", phase, error)
        return False
    finally:
        with suppress(OSError):
            connection.settimeout(3)
            connection.sendall(b"DONE\n" if handed_off else b"ERR\n")
