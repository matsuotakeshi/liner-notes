"""Authentication boundary over real sockets, journal and authoritative SQLite."""
from contextlib import closing
from pathlib import Path
import sys
import tempfile

from test_board import Server, post, read
from test_members import auth, PASSWORD
from test_runtime import Client, test_wait_timeout
from test_stage58 import cmd


def rejected(server, name, password=PASSWORD):
    # A fresh connection isolates rejection from the existing credential throttle.
    with closing(Client(server.port)) as client:
        client.sock.settimeout(test_wait_timeout("LINER_TEST_AUTH_TIMEOUT", 10))
        assert cmd(client, "LOGIN " + name) == b"PASSWORD"
        assert cmd(client, password) == b"ERR authentication"
        assert cmd(client, "ADMIN") == b"ROLE guest"
        assert cmd(client, "POST forbidden") == b"ERR login required"


def scenario(binary, sqlite):
    with tempfile.TemporaryDirectory(prefix="authentication-core-") as directory:
        root = Path(directory)
        admins = root / "admins"
        options = ["--database", str(root / "store.sqlite")] if sqlite else []

        def start(extra=()):
            return Server(binary, root / "notes", members=root / "members",
                          admins=admins if admins.exists() else None, language="en",
                          options=options + list(extra))

        with closing(start()) as server:
            with closing(Client(server.port)) as admin:
                auth(admin, "admin", True)
                assert cmd(admin, "ADMIN") == b"ROLE member"
            with closing(Client(server.port)) as alice:
                auth(alice, "alice", True)
                assert cmd(alice, "PAGER ON") == b"PAGER ON"
                assert cmd(alice, "PREFS SAVE") == b"PREFS SAVED en ON"
                article = post(alice, "authentication persistence", "preserved body")
            rejected(server, "alice", "incorrect-password")
            rejected(server, "unknown")
        admins.write_text("1\n", encoding="utf-8")

        with closing(start(["--registration", "approval"])) as server:
            with closing(Client(server.port)) as admin:
                auth(admin, "admin")
                assert cmd(admin, "ADMIN") == b"ROLE admin"
                with closing(Client(server.port)) as alice:
                    auth(alice, "alice")
                    assert cmd(alice, "PAGER") == b"PAGER ON"
                    assert read(alice, article)[1:] == ("authentication persistence", "preserved body\r\n")
                assert cmd(admin, "SUSPEND 2") == b"SUSPENDED 2"
                rejected(server, "alice")
                assert cmd(admin, "RESUME 2") == b"RESUMED 2"
                with closing(Client(server.port)) as pending:
                    pending.sock.settimeout(test_wait_timeout("LINER_TEST_AUTH_TIMEOUT", 10))
                    assert cmd(pending, "REGISTER pending") == b"PASSWORD"
                    assert cmd(pending, PASSWORD) == b"APPLICATION SAVED"
                    assert cmd(pending, "ADMIN") == b"ROLE guest"
                    assert cmd(pending, "POST forbidden") == b"ERR login required"
                rejected(server, "pending")
                assert cmd(admin, "APPROVE 3") == b"RIGHTS 3 LEVEL 2 GROUPS - STATUS active"
                with closing(Client(server.port)) as approved:
                    auth(approved, "pending")
                    assert cmd(approved, "ADMIN") == b"ROLE member"

        with closing(start(["--registration", "closed"])) as server:
            with closing(Client(server.port)) as client:
                client.sock.settimeout(test_wait_timeout("LINER_TEST_AUTH_TIMEOUT", 10))
                assert cmd(client, "REGISTER closed") == b"PASSWORD"
                assert cmd(client, PASSWORD) == b"ERR registration closed"
                assert cmd(client, "ADMIN") == b"ROLE guest"
            with closing(Client(server.port)) as alice:
                auth(alice, "alice")
                assert cmd(alice, "PAGER") == b"PAGER ON"
            with closing(Client(server.port)) as approved:
                auth(approved, "pending")
        with closing(start()) as server, closing(Client(server.port)) as client:
            auth(client, "closed", True)
            # Closed registration did not reserve an ID; persisted approval did.
            assert cmd(client, "RIGHTS") == b"RIGHTS 4 LEVEL 2 GROUPS - STATUS active"
        print("PASS: authentication, approval, suspension, bootstrap, restart", "SQLite" if sqlite else "journal")


if __name__ == "__main__":
    executable = str(Path(sys.argv[1]).resolve())
    for authoritative in (False, True):
        scenario(executable, authoritative)
