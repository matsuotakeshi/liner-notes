"""Real BBS/Telnet acceptance for a separately prepared Solaris test service."""

import argparse
import os
from pathlib import Path
import re
import socket
import stat
import sys
import tempfile

from test_board import Server
from test_door_resize import naws
from test_telnet import Client


def accept(runtime, endpoint, mode):
    root = Path(endpoint).parent
    if not re.fullmatch(r"/var/db/ln-door-bbs-[a-zA-Z0-9_]+", str(root)) or endpoint.name != "isolation":
        raise ValueError("Use the disposable Solaris Door fixture endpoint")
    info = root.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
        raise ValueError("Expected a private fixture root controlled by the operator")
    with tempfile.TemporaryDirectory(prefix="ln-auth-door-") as scratch:
        area = Path(scratch)
        doors = area / "doors.conf"
        doors.write_text(f"3|0|30|/usr/bin/false|{area}|Solaris auth fixture\n", encoding="utf8")
        previous = Path.cwd()
        os.chdir(area)
        try:
            server = Server(str(runtime), area / "notes", members=area / "members", menu=True,
                            telnet=True, language="en", bind="127.0.0.1",
                            options=["--doors", str(doors), "--door-isolation-socket", str(endpoint)])
            try:
                client = Client(server.port)
                # The configured readiness deadline is 8 seconds.
                client.sock.settimeout(12)
                try:
                    client.until()
                    client.send(b"REGISTER fixture\r\nfixture-password\r\n")
                    assert b"Logged in as fixture" in client.until()
                    assert b"3: Solaris auth fixture" in client.cmd("GAME", b"0:main] > ")
                    if mode == "denied":
                        reply = client.cmd("3", b"0:main] > ")
                        assert b"AUTH LIVE" not in reply
                        assert b"3: Solaris auth fixture" in reply
                    else:
                        client.send(b"3\r\n")
                        client.until(b"AUTH LIVE")
                        naws(client, 100, 30)
                        client.send(b"size\r")
                        client.until(b"SIZE 100 30")
                        if mode == "quit":
                            client.send(b"quit\r")
                            assert b"Back to BBS" in client.until(b"0:main] > ")
                    print("BBS_DOOR_ACCEPTANCE " + mode, flush=True)
                finally:
                    client.close()
            finally:
                server.close()
        finally:
            os.chdir(previous)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runtime", type=Path)
    parser.add_argument("endpoint", type=Path)
    parser.add_argument("mode", choices=("quit", "disconnect", "denied"))
    args = parser.parse_args()
    if sys.platform != "sunos5" or os.getuid() != 61002 or os.geteuid() != 61002:
        parser.error("The disposable Solaris BBS UID is required")
    accept(args.runtime.resolve(strict=True), args.endpoint, args.mode)
