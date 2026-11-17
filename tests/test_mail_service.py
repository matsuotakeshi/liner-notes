"""Mail composition boundary: live recipient policy, restart and lower quotas."""
from contextlib import closing
from pathlib import Path
import sys
import tempfile

from test_board import Server
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd
from test_mail import listing, read
from test_mail_draft import info


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="mail-service-") as directory:
        root = Path(directory)
        board, members, admins = root / "notes", root / "members", root / "admins"
        server = Server(binary, board, members=members)
        try:
            for name in ("alice", "bob", "admin"):
                with closing(Client(server.port)) as client:
                    auth(client, name, True)
        finally:
            server.close()
        admins.write_text("3\n", encoding="utf-8")
        kwargs = dict(members=members, admins=admins)
        server = Server(binary, board, **kwargs)
        try:
            with closing(Client(server.port)) as alice, closing(Client(server.port)) as admin:
                auth(alice, "alice")
                auth(admin, "admin")
                assert cmd(alice, "MAIL SEND bob original") == b"MAIL EDIT end with . or /CANCEL"
                alice.send(b"private draft longer than sixteen bytes\n")
                assert cmd(alice, "/SAVE") == b"MAIL DRAFT SAVED"
                revision = info(alice)
                assert cmd(alice, "MAIL RESUME").startswith(b"MAIL DRAFT ")
                assert cmd(admin, "SUSPEND 2") == b"SUSPENDED 2"
                assert cmd(alice, ".") == b"MAIL ERROR unavailable"
                # Failed send leaves the editor and durable draft intact.
                assert cmd(alice, "/CANCEL") == b"CANCELLED"
                assert info(alice) == revision
                assert cmd(alice, "MAIL RESUME") == b"MAIL ERROR unavailable"
                assert cmd(admin, "MAIL RESUME") == b"MAIL ERROR unavailable"
                assert cmd(admin, "RESUME 2") == b"RESUMED 2"
                with closing(Client(server.port)) as bob:
                    auth(bob, "bob")
                    assert not any(row.startswith(b"MAIL ROW") for row in listing(bob))
        finally:
            server.close()

        # Disabling mail does not discard the actor's saved draft.
        server = Server(binary, board, options=["--disable", "mail"], **kwargs)
        try:
            with closing(Client(server.port)) as alice:
                auth(alice, "alice")
                assert cmd(alice, "MAIL RESUME") == b"ERR feature disabled"
        finally:
            server.close()

        limits = root / "limits.conf"
        limits.write_text("body_bytes=16\n", encoding="utf-8")
        server = Server(binary, board, options=["--limits", str(limits)], **kwargs)
        try:
            with closing(Client(server.port)) as alice:
                auth(alice, "alice")
                assert info(alice) == revision
                # Existing content remains resumable, but new writes obey the
                # current quota without consuming the durable draft on refusal.
                assert cmd(alice, "MAIL RESUME").startswith(b"MAIL DRAFT ")
                assert cmd(alice, ".") == b"ERR body limit"
                assert cmd(alice, "/SAVE") == b"ERR body limit"
                assert cmd(alice, "/CANCEL") == b"CANCELLED"
                assert info(alice) == revision
        finally:
            server.close()

        server = Server(binary, board, **kwargs)
        try:
            with closing(Client(server.port)) as alice, closing(Client(server.port)) as bob, closing(Client(server.port)) as admin:
                auth(alice, "alice")
                auth(bob, "bob")
                auth(admin, "admin")
                assert cmd(alice, "MAIL RESUME").startswith(b"MAIL DRAFT ")
                assert cmd(alice, ".") == b"MAIL SENT 1"
                assert cmd(alice, "MAIL RESUME") == b"MAIL ERROR unavailable"
                assert b"private draft longer than sixteen bytes" in read(bob, 1)
                assert cmd(admin, "MAIL REPLY 1") == b"MAIL ERROR unavailable"
                assert cmd(alice, "MAIL REPLY 1") == b"MAIL ERROR unavailable"
                assert cmd(bob, "MAIL REPLY 1").startswith(b"MAIL EDIT ")
                bob.send(b"reply body\n")
                assert cmd(bob, "/SAVE") == b"MAIL DRAFT SAVED"
                reply_revision = info(bob)
                assert cmd(alice, "MAIL DELETE 1").startswith(b"MAIL DELETE CONFIRM")
                assert cmd(alice, "Y") == b"MAIL DELETED 1"
                assert cmd(bob, "MAIL RESUME") == b"MAIL ERROR unavailable"
                assert info(bob) == reply_revision
                assert cmd(bob, f"MAIL DRAFT DELETE {reply_revision}") == b"MAIL DRAFT DELETED"
        finally:
            server.close()
    print("PASS mail Core integration: recipient state, persisted draft, quotas, private reply and cleanup")


if __name__ == "__main__":
    run(sys.argv[1])
