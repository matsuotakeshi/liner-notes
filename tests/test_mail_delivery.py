"""Socket regression for SYSOP routing, draft isolation and private transfer revocation."""
from contextlib import closing, ExitStack
import hashlib
from pathlib import Path
import sys
import tempfile

from test_board import Server
from test_runtime import Client
from test_members import auth, PASSWORD
from test_mail import read
from test_mail_draft import info
from test_stage58 import cmd

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from transfer_connectors import MailConnector, RemoteError


def sysop(client, title, body):
    client.send(("SYSOPMAIL " + title + "\n").encode())
    response = client.line()
    assert b"Compose mail" in response, response
    client.send((body + "\n.\n").encode())
    response = client.line()
    # Session renders the localized completion, not board.c's internal ID reply.
    assert response == b"SYSOP MAIL SENT", response


def denied(action):
    try:
        action()
    except RemoteError as error:
        assert str(error) in ("ERR no file", "ERR vault denied", "ERR session revoked", "ERR login required"), str(error)
    except ValueError as error:
        assert str(error) == "Connection closed", str(error)
    else:
        raise AssertionError("private transfer succeeded without authority")


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="mail-delivery-") as directory:
        root = Path(directory)
        admins = root / "admins"
        def start(disabled=False):
            options = ["--database", str(root / "liner.sqlite"), "--disable", "vault"]
            if disabled:
                options += ["--disable", "mail"]
            return Server(binary, root / "notes", members=root / "members",
                          admins=admins if admins.exists() else None, language="en", options=options)
        with closing(start()) as server:
            for name in ("alice", "bob", "carol"):
                with closing(Client(server.port)) as client:
                    auth(client, name, True)
        # Deliberately reversed: routing uses the lowest ID, not file order.
        admins.write_text("3\n2\n", encoding="utf-8")
        payload = b"private transfer data" * 8
        digest = hashlib.sha256(payload).hexdigest()
        with closing(start()) as server, ExitStack() as stack:
            alice, bob, carol, guest = [stack.enter_context(closing(Client(server.port))) for _ in range(4)]
            for client, name in ((alice, "alice"), (bob, "bob"), (carol, "carol")):
                auth(client, name)
            sysop(guest, "guest request", "guest private body")
            delivered = read(bob, 1)
            assert all(text in delivered for text in (b"FROM guest", b"guest request", b"guest private body")), delivered
            assert read(carol, 1) == b"MAIL ERROR unavailable"
            assert cmd(guest, "MAIL READ 1") == b"ERR login required"
            assert cmd(alice, "MAIL SEND carol retained").startswith(b"MAIL EDIT ")
            alice.send(b"retained draft body\n")
            assert cmd(alice, "/SAVE") == b"MAIL DRAFT SAVED"
            revision = info(alice)
            assert cmd(alice, "MAIL EXIT") == b"MAIL CLOSED"
            sysop(alice, "member request", "member private body")
            assert info(alice) == revision
            delivered = read(bob, 2)
            assert all(text in delivered for text in (b"FROM alice", b"member request", b"member private body")), delivered
            assert read(carol, 2) == b"MAIL ERROR unavailable"
            assert cmd(carol, "MAIL EXIT") == b"MAIL CLOSED"
            with closing(MailConnector("127.0.0.1", server.port, "alice", PASSWORD)) as sender:
                ident = sender.begin(len(payload), digest, "private.bin", "private description")
                assert sender.write(ident, 0, payload[:32]) == 32
                assert sender.write(ident, 0, payload[:32]) == 32
                assert cmd(carol, "SUSPEND 1") == b"SUSPENDED 1"
                denied(lambda: sender.write(ident, 32, payload[32:]))
                assert cmd(carol, "RESUME 1") == b"RESUMED 1"
            with closing(MailConnector("127.0.0.1", server.port, "alice", PASSWORD)) as sender:
                assert sender.status(ident)[1] == 32
                sender.write(ident, 32, payload[32:])
                assert sender.finish(ident) == (len(payload), digest)
                assert sender.info(ident) == (len(payload), digest)
                # Legacy FILE metadata retains names/descriptions as well as digest.
                wire = sender.protocol
                wire.send(f"FILE {ident}")
                response = [wire.line() for _ in range(4)]
                assert response[0] == f"FILE {ident} {len(payload)} {digest}".encode()
                assert response[1:] == [b"NAME private.bin", b"DESCRIPTION private description", b"END FILE"]
            with closing(MailConnector("127.0.0.1", server.port, "carol", PASSWORD)) as other:
                denied(lambda: other.info(ident))
        # The next boot has a different recipient policy; old private mail stays private.
        admins.write_text("3\n", encoding="utf-8")
        with closing(start()) as server, ExitStack() as stack:
            alice, bob, carol, guest = [stack.enter_context(closing(Client(server.port))) for _ in range(4)]
            for client, name in ((alice, "alice"), (bob, "bob"), (carol, "carol")):
                auth(client, name)
            assert info(alice) == revision
            sysop(guest, "new recipient", "carol private body")
            delivered = read(carol, 3)
            assert all(text in delivered for text in (b"FROM guest", b"new recipient", b"carol private body")), delivered
            assert read(bob, 3) == b"MAIL ERROR unavailable"
            assert read(carol, 1) == b"MAIL ERROR unavailable"
            assert b"guest private body" in read(bob, 1)
        with closing(start(disabled=True)) as server, closing(Client(server.port)) as guest, closing(Client(server.port)) as alice:
            assert cmd(guest, "SYSOPMAIL disabled") == b"ERR feature disabled"
            auth(alice, "alice")
            assert cmd(alice, "MAIL DRAFT") == b"ERR feature disabled"
    print("PASS SYSOP routing/draft isolation, private transfer revocation/restart and wire metadata")


if __name__ == "__main__":
    run(sys.argv[1])
