"""Application Core boundary through real sockets and persisted private notes."""
from contextlib import closing
from pathlib import Path
import sqlite3
import sys
import tempfile

from test_board import Server
from test_runtime import Client
from test_members import auth, PASSWORD
from test_stage58 import cmd, rows
from test_stage64_details import until


def apply(client, ident):
    client.send(b"REGISTER\n")
    fields = ["Jane Doe", "Kana", "Private Street", "012345", "2000-02-29",
              "X68000", "Handle", "Tokyo", "Coder", "Music",
              "https://example.org", "Contact", "Other BBS", "Private note"]
    for value in fields:
        until(client, b": ")
        client.send((value + "\n").encode())
    review = until(client, b"> ")
    assert b"Private Street" in review and b"Y:confirm" in review
    client.send(b"Y\n")
    client.expect(b"PASSWORD")
    assert cmd(client, PASSWORD) == f"APPLICATION SAVED u{ident:06d}".encode()


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="application-review-") as directory:
        root = Path(directory)
        board, members, admins = root / "notes", root / "members", root / "admins"
        server = Server(binary, board, members=members, language="en")
        try:
            for name in ("admin", "viewer"):
                with closing(Client(server.port)) as client:
                    auth(client, name, True)
        finally:
            server.close()
        admins.write_text("1\n", encoding="utf-8")
        options = ["--registration", "approval", "--id-mode", "auto",
                   "--registration-form", "detailed"]
        kwargs = dict(members=members, admins=admins, language="en", options=options)
        server = Server(binary, board, **kwargs)
        try:
            with closing(Client(server.port)) as admin, closing(Client(server.port)) as viewer, closing(Client(server.port)) as guest:
                auth(admin, "admin")
                auth(viewer, "viewer")
                apply(guest, 3)
                with closing(Client(server.port)) as other:
                    apply(other, 4)
                assert cmd(guest, "APPLICATION 3") == b"ERR login required"
                for command in ("APPLICATION 3", "APPLICATIONSET 3 SYSOPNOTE forbidden"):
                    assert cmd(viewer, command) == b"ERR admin required"
                details = rows(admin, "APPLICATION 3", b"END APPLICATION")
                for expected in (b"REALNAME Jane Doe", b"ADDRESS Private Street",
                                 b"BIRTHDAY 2000-02-29", b"FROMBBS Other BBS", b"SYSOPNOTE Private note"):
                    assert expected in details, details
                assert cmd(admin, "APPLICATIONSET 3 MACHINE FreeBSD") == b"PROFILE SAVED"
                assert cmd(admin, "APPLICATIONSET 3 FROMBBS " + "x" * 320) == b"APPLICATION SAVED"
                assert cmd(admin, "APPLICATIONSET 3 FROMBBS " + "x" * 321) == b"ERR command"
                assert cmd(admin, "APPLICATIONSET 3 SYSOPNOTE") == b"APPLICATION SAVED"
                assert b"SYSOPNOTE " in rows(admin, "APPLICATION 3", b"END APPLICATION")
                database = Path(str(board) + ".features.sqlite")
                assert database.is_file()

                def sql(statement, parameters=()):
                    with closing(sqlite3.connect(database)) as db:
                        db.execute(statement, parameters)
                        db.commit()

                # Deterministic fixture fault: a rejected SQLite write must not
                # acknowledge success or replace previously persisted notes.
                sql("CREATE TRIGGER reject_note BEFORE UPDATE ON applications "
                    "WHEN NEW.user=3 BEGIN SELECT RAISE(ABORT,'fixture write failure'); END")
                assert cmd(admin, "APPLICATIONSET 3 FROMBBS rejected") == b"ERR settings storage"
                sql("DROP TRIGGER reject_note")
                assert b"FROMBBS " + b"x" * 320 in rows(admin, "APPLICATION 3", b"END APPLICATION")

                # A late corrupt row must not leak an earlier profile/note row.
                sql("INSERT INTO applications VALUES(3,'UNKNOWN','fixture')")
                assert cmd(admin, "APPLICATION 3") == b"ERR settings storage"
                assert cmd(admin, "PING") == b"PONG"
                sql("DELETE FROM applications WHERE user=3 AND field='UNKNOWN'")
                sql("UPDATE applications SET value=? WHERE user=3 AND field='SYSOPNOTE'", ("z" * 321,))
                assert cmd(admin, "APPLICATION 3") == b"ERR settings storage"
                assert cmd(admin, "PING") == b"PONG"
                sql("UPDATE applications SET value='' WHERE user=3 AND field='SYSOPNOTE'")
        finally:
            server.close()

        # Review eligibility is re-evaluated after restart and every lifecycle
        # operation; profile-field aliases retain their existing admin meaning.
        server = Server(binary, board, **kwargs)
        try:
            with closing(Client(server.port)) as admin, closing(Client(server.port)) as applicant:
                auth(admin, "admin")
                details = rows(admin, "APPLICATION 3", b"END APPLICATION")
                assert b"MACHINE FreeBSD" in details and b"SYSOPNOTE " in details
                assert b"FROMBBS " + b"x" * 320 in details
                assert cmd(admin, "APPROVE 3").startswith(b"RIGHTS")
                assert cmd(admin, "APPLICATION 3") == b"ERR command"
                assert cmd(admin, "APPLICATIONSET 3 SYSOPNOTE denied") == b"ERR command"
                assert cmd(admin, "APPLICATIONSET 3 MACHINE Approved machine") == b"PROFILE SAVED"
                auth(applicant, "u000003")
                assert b"MACHINE Approved machine" in rows(applicant, "PROFILE", b"END PROFILE")
                assert cmd(admin, "REJECT 4 CONFIRM") == b"MEMBER REMOVED 4 KIND 1"
                assert cmd(admin, "APPLICATION 4") == b"ERR command"
                assert cmd(admin, "APPLICATIONSET 4 FROMBBS denied") == b"ERR command"
        finally:
            server.close()
    print("PASS application review: private fields, ACL, notes, persistence, lifecycle and SQLite faults")


if __name__ == "__main__":
    run(sys.argv[1])
