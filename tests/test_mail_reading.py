"""Actual socket coverage for mail list/search/read Core and participant ACLs."""
from contextlib import closing
from pathlib import Path
import sys
import tempfile

from test_board import Server
from test_runtime import Client
from test_members import auth
from test_mail import send, read, listing
from test_stage58 import cmd


def ids(rows):
    return [int(row.split()[2]) for row in rows if row.startswith(b"MAIL ROW ")]


def pages(client, command):
    result = []
    for _ in range(8):
        rows = listing(client, command)
        result.extend(ids(rows))
        if rows[-1] == b"MAIL END NEXT 0":
            return result
        command = "+"
    raise AssertionError("mail page cursor did not terminate")


def item(client, command):
    client.send((command + "\n").encode())
    result = []
    while True:
        row = client.line()
        result.append(row)
        if row in (b"MAIL END", b"MAIL SCAN END") or row.startswith(b"MAIL ERROR"):
            return result


def sequence(client, descending=False):
    command = "MAIL SCAN DESC" if descending else "MAIL SCAN ASC"
    result = []
    for _ in range(24):
        rows = item(client, command)
        if rows == [b"MAIL SCAN END"]:
            return result
        header = next(row for row in rows if row.startswith(b"MAIL ITEM "))
        result.append(int(header.split()[2]))
        assert b"private-secret" not in b"\n".join(rows)
        command = "CONTINUE"
    raise AssertionError("mail sequence did not terminate")


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="mail-reading-") as directory:
        root = Path(directory)
        board, members, admins = root / "notes", root / "members", root / "admins"
        server = Server(binary, board, members=members)
        try:
            for name in ("alice", "bob", "carol"):
                with closing(Client(server.port)) as client:
                    auth(client, name, True)
        finally:
            server.close()
        admins.write_text("3\n", encoding="utf-8")
        kwargs = dict(members=members, admins=admins)
        server = Server(binary, board, **kwargs)
        try:
            with closing(Client(server.port)) as alice, closing(Client(server.port)) as bob, closing(Client(server.port)) as admin:
                auth(alice, "alice")
                auth(bob, "bob")
                auth(admin, "carol")
                own = []
                private = []
                for n in range(10):
                    own.append(send(alice, "bob", f"subject-{n}", f"Needle body {n}"))
                    if n == 3:
                        private.append(send(bob, "carol", "private-secret", "Needle private-secret"))
                private.append(send(bob, "carol", "private-secret-last", "Needle private-secret"))
                incoming = send(bob, "alice", "incoming", "inbox-body")
                first = listing(alice, "MAIL SEARCH Needle")
                assert ids(first) == own[:8], first
                # Direct read and rejected requests must not replace the list
                # cursor or query. Selection is not authorization for another mail.
                assert item(alice, f"MAIL READ {own[0]}")[0].startswith(f"MAIL ITEM {own[0]} ".encode())
                assert read(alice, private[0]) == b"MAIL ERROR unavailable"
                assert cmd(alice, "MAIL SEARCHFROM 4294967296 Changed") == b"ERR command"
                assert ids(listing(alice, "+")) == own[8:]
                assert pages(alice, "MAIL SEARCH private-secret") == []
                assert pages(alice, "MAIL SEARCH needle") == []
                assert pages(admin, "MAIL SEARCH Needle") == private
                assert pages(alice, "MAIL SENT") == own
                assert pages(alice, "MAIL INBOX") == [incoming]
                assert pages(alice, "MAIL UNREAD") == [incoming]
                assert pages(alice, "MAIL NEWS 1970-01-01") == [incoming]
                assert pages(alice, "MAIL NEWS 9999-12-31") == []
                assert read(alice, private[0]) == b"MAIL ERROR unavailable"
                assert read(admin, own[0]) == b"MAIL ERROR unavailable"
                latest = item(alice, "MAIL LATEST")
                assert latest[0].startswith(f"MAIL ITEM {incoming} ".encode()), latest
                assert b"READ 1" in latest[0]
                assert item(alice, "MAIL NEXT") == [b"MAIL ERROR unavailable"]
                assert cmd(alice, f"MAIL DELETE {own[0]}").startswith(b"MAIL DELETE CONFIRM")
                assert cmd(alice, "n") == b"CANCELLED"
                assert item(alice, f"MAIL READ {own[0]}")[0].startswith(f"MAIL ITEM {own[0]} ".encode())
                assert cmd(alice, f"MAIL DELETE {own[1]}").startswith(b"MAIL DELETE CONFIRM")
                assert cmd(alice, "Y") == f"MAIL DELETED {own[1]}".encode()
                visible = [ident for ident in own if ident != own[1]] + [incoming]
                assert sequence(alice) == visible
                assert sequence(alice, True) == list(reversed(visible))
                # Reading sent mail as sender must not mark it for the recipient.
                assert pages(bob, "MAIL UNREAD") == visible[:-1]
        finally:
            server.close()
        server = Server(binary, board, **kwargs)
        try:
            with closing(Client(server.port)) as alice, closing(Client(server.port)) as bob:
                auth(alice, "alice")
                auth(bob, "bob")
                assert pages(alice, "MAIL LIST") == visible
                assert pages(alice, "MAIL UNREAD") == []
                assert read(alice, own[1]) == b"MAIL ERROR unavailable"
                next_unread = item(bob, "MAIL NEXT")
                assert next_unread[0].startswith(f"MAIL ITEM {own[0]} ".encode())
                assert b"READ 1" in next_unread[0]
                assert pages(bob, "MAIL UNREAD") == visible[1:-1]
        finally:
            server.close()
    print("PASS mail reading: private paging/search, dates, unread/latest, sequences and restart")


if __name__ == "__main__":
    run(sys.argv[1])
