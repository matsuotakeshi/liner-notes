"""Socket regression for owned attachment views and private common-buffer import."""
from contextlib import ExitStack, closing
import hashlib
from pathlib import Path
import sys
import tempfile

from test_board import Server, read as article_read
from test_runtime import Client
from test_members import auth, PASSWORD
from test_mail import listing, read
from test_stage58 import cmd
from test_talk import command

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from transfer_connectors import MailConnector, RemoteError


def files(client):
    rows, cursor = [], 0
    for _ in range(8):
        # The Core accepts an initial zero cursor, but the text command's
        # explicit number argument must be positive. Omit it on the first page.
        request = f"MAIL FILES {cursor}" if cursor else "MAIL FILES"
        client.send((request + "\n").encode())
        header = client.line()
        assert header == b"MAIL FILES", (request, header)
        while True:
            line = client.line()
            if line.startswith(b"END MAILFILES NEXT "):
                next_cursor = int(line.split()[-1])
                break
            assert line.startswith(b"MAILFILE "), line
            rows.append(line)
        if not next_cursor:
            return rows
        assert next_cursor > cursor
        cursor = next_cursor
    raise AssertionError("attachment page cursor did not terminate")


def imported(client, expected):
    assert cmd(client, "MAIL EXIT") == b"MAIL CLOSED"
    assert cmd(client, "POST imported").startswith(b"EDIT")
    assert cmd(client, "/BUFFER") == b"BUFFER IMPORTED"
    saved = cmd(client, ".")
    assert saved.startswith(b"SAVED "), saved
    assert article_read(client, int(saved.split()[1]))[2] == expected


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="mail-resources-") as folder:
        root = Path(folder)
        admins = root / "admins"
        options = ["--disable", "vault", "--database", str(root / "liner.sqlite")]
        def server():
            return Server(binary, root / "notes", members=root / "members",
                          admins=admins if admins.exists() else None, options=options)
        with closing(server()) as s, ExitStack() as stack:
            for name in ("alice", "bob", "carol"):
                c = stack.enter_context(closing(Client(s.port)))
                auth(c, name, True)
        admins.write_text("3\n", encoding="utf-8")
        payload = b"private blob"
        digest = hashlib.sha256(payload).hexdigest()
        body = "Private mixed case\r\nSecond line\r\n"
        with closing(server()) as s, ExitStack() as stack:
            a, b, c = [stack.enter_context(closing(Client(s.port))) for _ in range(3)]
            for client, name in ((a, "alice"), (b, "bob"), (c, "carol")):
                auth(client, name)
            ca, cb, cc = [stack.enter_context(closing(MailConnector("127.0.0.1", s.port, name, PASSWORD)))
                          for name in ("alice", "bob", "carol")]
            def upload(connector, name):
                ident = connector.begin(len(payload), digest, name, "private")
                connector.write(ident, 0, payload)
                connector.finish(ident)
                return ident
            owned = []
            for i in range(10):
                owned.append(upload(ca, f"alice-{i}.bin"))
                if i == 1:
                    other = upload(cb, "bob-only.bin")
            active = ca.begin(len(payload), digest, "unfinished.bin", "")
            cancelled = ca.begin(len(payload), digest, "cancelled.bin", "")
            command(a, f"MAIL DROP {cancelled} CONFIRM", b"MAIL ATTACHMENT REMOVED")
            rows = files(a)
            assert [int(row.split()[1]) for row in rows] == owned + [active], rows
            assert rows[-1].endswith(b"STATE ACTIVE NAME -")
            assert all(b"STATE READY NAME alice-" in row for row in rows[:-1])
            assert [int(row.split()[1]) for row in files(b)] == [other]
            assert files(c) == []
            command(a, "MAIL SEND bob private", b"MAIL EDIT ")
            for ident in owned[:2]:
                command(a, f"/ATTACH {ident}", b"MAIL ATTACHMENT ")
            a.send(body.replace("\r\n", "\n").encode())
            command(a, "/SAVE", b"MAIL DRAFT SAVED")
            a.send(b"MAIL RESUME\n")
            for ident in owned[:2]:
                assert a.line().startswith(f"MAIL ATTACHMENT {ident} SIZE ".encode())
            assert a.line().startswith(b"MAIL DRAFT ")
            command(a, ".", b"MAIL SENT 1")
            assert b"UNREAD 1" in listing(b)[0]
            assert cmd(b, "MAIL GET 1") == b"MAIL IMPORTED TO BUFFER"
            assert b"UNREAD 1" in listing(b)[0]
            assert cmd(c, "MAIL GET 1") == b"MAIL ERROR unavailable"
            imported(b, body)
            assert cb.info(owned[0]) == (len(payload), digest)
            assert [int(row.split()[1]) for row in files(b)] == [other]
            try:
                cc.info(owned[0])
            except RemoteError:
                pass
            else:
                raise AssertionError("administrator read private attachment")
            contents = read(b, 1)
            for ident in owned[:2]:
                assert f"MAIL ATTACHMENT {ident} SIZE ".encode() in contents
            assert body.strip().splitlines()[0].encode() in contents
        with closing(server()) as s, ExitStack() as stack:
            a, b, c = [stack.enter_context(closing(Client(s.port))) for _ in range(3)]
            for client, name in ((a, "alice"), (b, "bob"), (c, "carol")):
                auth(client, name)
            assert [int(row.split()[1]) for row in files(a)] == owned + [active]
            assert files(c) == []
            # Persisted buffer can still be used after restart without re-reading mail.
            command(b, "MAIL", b"MAIL LIST")
            while not b.line().startswith(b"MAIL END NEXT "):
                pass
            imported(b, body)
            command(b, "MAIL DELETE 1", b"MAIL DELETE CONFIRM")
            command(b, "Y", b"MAIL DELETED 1")
            assert cmd(b, "MAIL GET 1") == b"MAIL ERROR unavailable"
            assert not any(f"MAILFILE {ident} ".encode() in row for row in files(a) for ident in owned[:2])
    print("PASS attachment owner pagination, recipient/admin ACL, draft/read metadata, buffer import and restart")


if __name__ == "__main__":
    run(sys.argv[1])
