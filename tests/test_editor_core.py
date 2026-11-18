"""Shared article/mail editor operations and durable drafts over real sockets."""
from contextlib import closing
from pathlib import Path
import sys
import tempfile

from test_board import Server, read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd
from test_preview import preview
from test_mail import read as read_mail


def scenario(binary, sqlite):
    with tempfile.TemporaryDirectory(prefix="editor-core-") as directory:
        root = Path(directory)
        options = ["--database", str(root / "store.sqlite")] if sqlite else []

        def start():
            return Server(binary, root / "notes", members=root / "members", language="en", options=options)

        with closing(start()) as server, closing(Client(server.port)) as a, closing(Client(server.port)) as b:
            auth(a, "alice", True)
            auth(b, "bob", True)
            assert cmd(a, "POST original").startswith(b"EDIT ")
            a.send("one needle needle\n日本語 needle\nlast\n".encode())
            initial = "one needle needle\r\n日本語 needle\r\nlast\r\n".encode()
            assert preview(a)[2] == initial
            assert cmd(a, "/TITLE changed") == b"DRAFT TITLE CHANGED"
            assert cmd(a, "/TITLE changed") == b"DRAFT TITLE CHANGED"
            assert cmd(a, "/UNDO") == b"DRAFT UNDONE"
            assert preview(a)[1] == b"TITLE original"
            assert cmd(a, "/EDITOR LINE").startswith(b"Line editor.")
            a.send(b"S1-2 needle\n")
            assert b"one needle needle" in a.line()
            assert "日本語 needle".encode() in a.line()
            assert a.line() == b"Matches: 3"
            a.send(b"Q1-2\n")
            quoted = "> one needle needle\r\n> 日本語 needle\r\nlast\r\n".encode()
            assert preview(a)[2] == quoted
            a.send(b"D2-3\n")
            assert preview(a)[2] == b"> one needle needle\r\n"
            assert cmd(a, "/UNDO") == b"DRAFT UNDONE"
            assert preview(a)[2] == quoted
            assert cmd(a, "Q2-4") == b"ERR draft line"
            assert preview(a)[2] == quoted
            assert cmd(a, "/UNDO") == b"ERR no undo"
            assert cmd(a, "/EDITOR BETA").startswith(b"Beta editor.")
            assert cmd(a, "/SAVE").startswith(b"DRAFT STORED ")
            assert cmd(a, "/CANCEL") == b"CANCELLED"
            assert cmd(b, "POST separate").startswith(b"EDIT ")
            assert cmd(b, "/UNDO") == b"ERR no undo"
            assert preview(b)[2] == b""
            assert cmd(b, "/CANCEL") == b"CANCELLED"
            assert cmd(a, "MAIL SEND bob private").startswith(b"MAIL EDIT ")
            a.send(b"private\nsecond\n")
            assert cmd(a, "/INSERT 2 middle") == b"DRAFT LINE INSERTED"
            assert cmd(a, "/UNDO") == b"DRAFT UNDONE"
            assert preview(a)[2] == b"private\r\nsecond\r\n"
            assert cmd(a, "/REPLACE 2 changed") == b"DRAFT LINE REPLACED"
            assert cmd(a, "/SAVE").startswith(b"MAIL DRAFT SAVED")
        with closing(start()) as server, closing(Client(server.port)) as a, closing(Client(server.port)) as b:
            auth(a, "alice")
            auth(b, "bob")
            assert cmd(a, "RESUME").startswith(b"DRAFT RESUMED")
            assert preview(a)[2] == quoted
            assert cmd(a, "/UNDO") == b"ERR no undo"
            assert cmd(a, ".") == b"SAVED 1"
            assert read(b, 1)[2].encode() == quoted
            assert cmd(a, "MAIL RESUME").startswith(b"MAIL DRAFT ")
            assert preview(a)[2] == b"private\r\nchanged\r\n"
            assert cmd(a, "/UNDO") == b"ERR no undo"
            assert cmd(a, ".") == b"MAIL SENT 1"
            message = read_mail(b, 1)
            assert b"private" in message and b"changed" in message and b"middle" not in message
        print("PASS: editor article/mail mutations, search, undo isolation and restart", "SQLite" if sqlite else "journal")


if __name__ == "__main__":
    executable = str(Path(sys.argv[1]).resolve())
    for authoritative in (False, True):
        scenario(executable, authoritative)
