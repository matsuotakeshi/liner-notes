"""One disposable game-server consumer for the Solaris BBS acceptance fixture."""

import argparse
import os
from pathlib import Path
import re
import socket
import sys
import time

sys.path.insert(0, str(Path(__file__).absolute().parents[1] / "scripts"))
from door_auth_client import AuthorizationError, DoorAuthorization
from door_broker import peer_uid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("channel")
    parser.add_argument("broker")
    parser.add_argument("--withhold", action="store_true", help="acknowledge the game without consuming the ticket")
    args = parser.parse_args()
    if sys.platform != "sunos5" or os.getuid() != 61004 or os.geteuid() != 61004:
        parser.error("The disposable Solaris consumer UID is required")
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as listener:
        listener.bind(args.channel)
        os.chmod(args.channel, 0o666)  # the parent directory is limited to the fixture roles
        listener.listen(1)
        listener.settimeout(12)
        print("CONSUMER READY", flush=True)
        connection, _ = listener.accept()
        with connection, connection.makefile("rb") as stream:
            if peer_uid(connection) != 61001:
                raise ValueError("Unexpected fixture Door UID")
            connection.settimeout(12)
            raw = stream.readline(66)
            if not re.fullmatch(rb"[0-9a-f]{64}\n", raw):
                raise ValueError("Unexpected fixture credential frame")
            if not args.withhold:
                lease = DoorAuthorization(args.broker, bbs="solaris-fixture", door=3,
                                          ticket=raw[:-1].decode("ascii"), broker_uid=61003)
                if lease.authorize(1)["member"] != 1:
                    raise ValueError("Unexpected fixture member")
                print("LEASE LIVE", flush=True)
            connection.sendall(b"L")
            if stream.read(1):
                raise ValueError("Unexpected data from fixture Door")
        if not args.withhold:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                try:
                    lease.authorize(1)
                except AuthorizationError:
                    print("LEASE REVOKED", flush=True)
                    return
                time.sleep(0.05)
            raise TimeoutError("Fixture lease remained usable")
        print("TICKET WITHHELD", flush=True)


if __name__ == "__main__":
    main()
