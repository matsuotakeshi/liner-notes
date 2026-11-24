"""Shared MMM/RTBBS line conference on real connections and a durable SQLite DB."""
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing, ExitStack
from pathlib import Path
import os
import sqlite3
import subprocess
import sys
import tempfile

from test_board import Server
from test_members import PASSWORD, auth
from test_runtime import Client as Raw
from test_telnet import Client, disconnected

MAIN = b"MAIN>> "
ROOMS = b"TALK) "
ROOM = b"TALK> "
PROMPT = b"] > "
TOOL = Path(__file__).resolve().parents[1] / "scripts" / "maintenance.py"


def login(client, name, mmm=False):
    client.until(PROMPT)
    client.cmd("LOGIN " + name, b"> ")
    client.cmd(PASSWORD, MAIN if mmm else PROMPT)


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="ln-conference-") as directory:
        scratch = Path(directory)
        root = scratch / "instance"
        root.mkdir()
        store = root / "notes"
        members = root / "members"
        options = ["--database-structured", str(root / "liner.sqlite")]

        with closing(Server(binary, store, members=members, options=options)) as server:
            for name in ("alice", "bob", "guest"):
                with closing(Raw(server.port)) as client:
                    auth(client, name, True)
            with closing(Raw(server.port)) as anonymous:
                anonymous.send(b"LINECONF\n")
                anonymous.expect(b"ERR conference login required")
        (root / "admins").write_text("1\n", encoding="ascii")

        def start(data=root, default="rtbbs"):
            return Server(binary, data/"notes", members=data/"members", admins=data/"admins",
                          telnet=True, menu=True, language="en",
                          options=["--database-structured", str(data/"liner.sqlite"),
                                   "--ui-policy", "MEMBERS", "--ui-default", default, "--ui-allow", "rtbbs,mmm"])

        with closing(start()) as server, ExitStack() as clients:
            def connect(name=None):
                client = clients.enter_context(closing(Client(server.port)))
                if name:
                    login(client, name)
                return client

            anonymous = connect()
            anonymous.until(PROMPT)
            assert b"Log in to use the line conference." in anonymous.cmd("LINECONF")
            assert b"Log in to use the line conference." in anonymous.cmd("LINECONF POST 1 anonymous")
            anonymous.cmd("LANG ja")
            assert "一行会議を利用するにはログインしてください。".encode() in anonymous.cmd("LINECONF")
            guest = connect("guest")
            assert b"Line conference access denied." in guest.cmd("LINECONF")
            assert b"Line conference access denied." in guest.cmd("LINECONF POST 1 guest")
            alice, bob = connect("alice"), connect("bob")
            assert b"ROOM 1 General" in alice.cmd("LINECONF")
            assert b"LINECONF NEW" in alice.cmd("HELP LINECONF")
            assert b"Line conference access denied." in bob.cmd("LINECONF CREATE 2 Lounge")
            assert b"CONFERENCE CREATED 2" in alice.cmd("LINECONF CREATE 2 Lounge")
            alice.cmd("UI mmm", b"UI preference saved.")
            alice.until(MAIN)
            assert b"Lounge" in alice.cmd("talk", ROOMS)
            alice.cmd("1", ROOM)
            assert b"Posted #1" in alice.cmd("MMM line: こんにちは", ROOM)
            assert b"MMM line:" in bob.cmd("lineconf history 1")
            assert b"CONFERENCE SAVED 2" in bob.cmd("LINECONF POST 2 RTBBS line: Case Preserved")
            alice.cmd("/q", MAIN)
            unread = alice.cmd("talk -n", ROOMS)
            assert b"MMM line:" in unread and b"RTBBS line: Case Preserved" in unread, unread
            assert b"Read through #2" in alice.cmd("a", ROOMS)
            alice.cmd("q", MAIN)
            assert b"No conference lines" in alice.cmd("talk -n", ROOMS)
            alice.cmd("q", MAIN)
            assert b"CONFERENCE POSITION 1 0" in bob.cmd("LINECONF POSITION 1")
            assert b"CONFERENCE POSITION 2 0" in bob.cmd("LINECONF POSITION 2")
            assert b"CONFERENCE POSITION 1 1" in bob.cmd("LINECONF SEEN 1 1")
            assert b"CONFERENCE POSITION 2 0" in bob.cmd("LINECONF POSITION 2")
            alice.cmd("ui rtbbs", b"UI PREF SAVED rtbbs")
            alice.until(PROMPT)
            assert b"CONFERENCE POSITION 2 2" in alice.cmd("LINECONF POSITION 2")

            # Different live sessions use the same serialized worker. Post and
            # acknowledge out of order without replacing a newer read marker.
            bob2 = connect("bob")
            with ThreadPoolExecutor(max_workers=2) as workers:
                one = workers.submit(alice.cmd, "LINECONF POST 1 concurrent Alice")
                two = workers.submit(bob2.cmd, "LINECONF POST 1 concurrent Bob")
                results = [one.result(), two.result()]
                assert sum(b"CONFERENCE SAVED 3" in result for result in results)==1
                assert sum(b"CONFERENCE SAVED 4" in result for result in results)==1
            assert b"CONFERENCE POSITION 1 4" in bob2.cmd("LINECONF SEEN 1 4")
            assert b"CONFERENCE POSITION 1 4" in bob.cmd("LINECONF SEEN 1 1")
            history = bob.cmd("LINECONF HISTORY 1")
            assert history.count(b"concurrent Alice")==1 and history.count(b"concurrent Bob")==1

            # Explicit failure must surface to the client without incrementing IDs.
            assert b"Check the LINECONF command." in bob.cmd("LINECONF POST 1 " + "x"*513)
            with closing(sqlite3.connect(root/"liner.sqlite")) as db, db:
                db.execute("CREATE TRIGGER fail_conference BEFORE INSERT ON conference_lines "
                           "BEGIN SELECT RAISE(ABORT,'test write failure'); END;")
            try:
                answer = bob.cmd("LINECONF POST 1 not durable")
                assert b"The line conference storage operation failed." in answer and b"SAVED" not in answer
            finally:
                with closing(sqlite3.connect(root/"liner.sqlite")) as db, db:
                    db.execute("DROP TRIGGER fail_conference")
            assert b"not durable" not in alice.cmd("LINECONF HISTORY 1")

            # A private RTBBS TALK exchange remains outside the conference.
            client = clients.enter_context(closing(Client(server.port)))
            greeting = client.until(PROMPT)
            target = greeting.split(b"RTBBS-PROBE ")[1].split()[0]
            client.cmd("LOGIN bob", b"> ")
            client.cmd(PASSWORD, PROMPT)
            alice.cmd("TALK " + target.decode(), b"Talk #" + target + b"> ")
            alice.cmd("private conference fixture", b"Talk #" + target + b"> ")
            alice.cmd("//", PROMPT)
            assert b"private conference fixture" not in bob.cmd("LINECONF HISTORY 1")
            assert b"SUSPENDED 2" in alice.cmd("SUSPEND 2")
            disconnected(bob)
            disconnected(bob2)
            rejected = connect()
            rejected.until(PROMPT)
            rejected.cmd("LOGIN bob", b"> ")
            rejected.cmd(PASSWORD, PROMPT)
            assert b"Log in to use the line conference." in rejected.cmd("LINECONF")

        with closing(start()) as server, closing(Client(server.port)) as alice:
            login(alice, "alice")
            assert b"CONFERENCE POSITION 2 2" in alice.cmd("LINECONF POSITION 2")
            assert b"concurrent Bob" in alice.cmd("LINECONF HISTORY 1")
            # Bob's pre-suspension read marker is also durable.
            assert b"RESUMED 2" in alice.cmd("RESUME 2")
            with closing(Client(server.port)) as bob:
                login(bob, "bob")
                assert b"CONFERENCE POSITION 1 4" in bob.cmd("LINECONF POSITION 1")
                assert b"CONFERENCE POSITION 2 0" in bob.cmd("LINECONF POSITION 2")

        if os.name == "posix":
            archive = scratch / "backup.tar.gz"
            restored = scratch / "restored"
            subprocess.run([sys.executable, str(TOOL), "db-backup",
                            "--root", str(root), "--database", str(root/"liner.sqlite"),
                            "--board", "notes", "--members", "members", str(archive)], check=True, capture_output=True)
            subprocess.run([sys.executable, str(TOOL), "db-restore", str(archive), str(restored)],
                           check=True, capture_output=True)
            with closing(start(restored)) as server, closing(Client(server.port)) as client:
                login(client, "alice")
                assert b"CONFERENCE POSITION 2 2" in client.cmd("LINECONF POSITION 2")
                assert b"RTBBS line: Case Preserved" in client.cmd("LINECONF HISTORY 2")
                assert b"CONFERENCE SAVED 5" in client.cmd("LINECONF POST 2 after restore")
        print("PASS MMM/RTBBS shared line conference: both directions, isolated read state, concurrent writes, denial, restart and backup")


if __name__ == "__main__":
    run(sys.argv[1])
