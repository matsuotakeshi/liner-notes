"""Second MMM terminal slice over real Telnet sockets and persistent stores."""
from contextlib import closing
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import sys
import tempfile

from test_board import Server
from test_mmm_ui import GATE, MAIN, NOTES, PASSWORD, post
from test_telnet import Client, disconnected

MAIL = b"MAIL>> "
FILER = b"FILER>> "
CHAT = b"CHAT>> "
TERM = b"TERM>> "
GAMES = b"GAMES>> "
NEW = b"NEW>> "
SECRET = "mmm-services-password"


def login(client, name, register=False):
    client.until(GATE)
    client.cmd(("register " if register else "login ") + name, PASSWORD)
    assert ("Logged in: " + name).encode() in client.cmd(SECRET, MAIN)


def run(binary, vaultctl, profile="mmm"):
    binary, vaultctl = (str(Path(path).resolve()) for path in (binary, vaultctl))
    with tempfile.TemporaryDirectory(prefix="ln-mmm-services-") as tmp:
        root = Path(tmp)
        vault = root / "vault"
        source = root / "source"
        source.write_bytes(b"MMM fixture\n")
        subprocess.run([vaultctl, "init", vault], check=True, capture_output=True)
        subprocess.run([vaultctl, "import", vault, source, "Manual.txt", "fixture"],
                       check=True, capture_output=True)
        game = root / "game"
        game.write_text('#!/bin/sh\nprintf "GAME_USER=%s:%s\\n" "$LINER_USER_ID" "$LINER_USER"\n'
                        'IFS= read -r reply\nprintf "ANSWER=%s\\n" "$reply"\n', encoding="utf-8")
        game.chmod(0o700)
        doors = root / "doors"
        doors.write_text(f"1|0|10|{game}|{root}|Fixture game\n2|5|10|{game}|{root}|Restricted\n",
                         encoding="utf-8")
        helper = root / "transfer-helper"
        helper.write_text('#!/bin/sh\nprintf "HELPER=%s:%s:%s\\n" "$1" "$2" "$3"\n'
                          'printf "DONE OK 1 12\\n" >&3\n', encoding="utf-8")
        helper.chmod(0o700)
        database = root / "bbs.sqlite"

        def start():
            return Server(binary, root / "notes", members=root / "members", vault=vault,
                          zhelper=helper, telnet=True, menu=True, language="en",
                          options=["--database", str(database), "--doors", str(doors),
                                   "--ui-default", profile, "--ui-allow", "rtbbs," + profile])

        def checkpoints():
            with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True)) as db:
                return db.execute("SELECT user,kind,stamp FROM news ORDER BY user,kind").fetchall()

        with closing(start()) as server:
            with closing(Client(server.port)) as alice, closing(Client(server.port)) as bob:
                login(alice, "alice", True)
                login(bob, "bob", True)
                bob.cmd("mail", MAIL)
                assert b"notifications: off" in bob.cmd("notify off", MAIL)
                bob.cmd(".", MAIN)

                alice.cmd("regsign -e", b"Editor>> ")
                alice.cmd("a", b"Text> ")
                alice.cmd("Alice signature", b"Text> ")
                alice.cmd(".", b"Editor>> ")
                assert b"Signature saved" in alice.cmd("w", MAIN)
                assert b"automatic" in alice.cmd("regsign -a", MAIN)
                assert b"No messages" in alice.cmd("mail", MAIL)
                alice.cmd("send bob", b"Title> ")
                alice.cmd("MMM mail", b"Text> ")
                alice.cmd("private text", b"Text> ")
                alice.cmd(".", b"Editor>> ")
                assert b"Mail draft saved" in alice.cmd("s", b"Editor>> ")
                alice.cmd("q", MAIL)
                assert b"private text" in alice.cmd("resume", b"Editor>> ") + alice.cmd("p", b"Editor>> ")
                assert b"Mail sent" in alice.cmd("w", MAIL)
                assert b"MMM mail" in alice.cmd("sent", MAIL)
                assert b"MMM mail" in bob.cmd("1", MAIL)
                assert b"MMM mail" in bob.cmd("find private text", MAIL)
                assert b"MMM mail" in bob.cmd("news 2000-01-01", MAIL)
                assert b"unavailable" in bob.cmd("killmail 1 confirm", MAIL)
                delivered = bob.cmd("read 1", MAIL)
                assert b"private text" in delivered and b"Alice signature" in delivered
                assert b"personal file" in bob.cmd("save 1", MAIL)
                assert b"unavailable" in alice.cmd("killmail 1 confirm", MAIL)
                bob.cmd("delete 1 confirm", MAIL)
                assert b"No messages" in bob.cmd("inbox", MAIL)
                alice.cmd("send bob", b"Title> ")
                alice.cmd("retractable", b"Text> ")
                alice.cmd("not seen", b"Text> ")
                alice.cmd(".", b"Editor>> ")
                assert b"Mail sent" in alice.cmd("w", MAIL)
                assert b"retracted" in alice.cmd("killmail 2 confirm", MAIL)
                assert b"retractable" not in bob.cmd("inbox", MAIL)
                alice.cmd(".", MAIN)
                bob.cmd(".", MAIN)

                alice.cmd("filer", FILER)
                assert b"Manual.txt" in alice.cmd("list", FILER)
                assert b"Manual.txt" in alice.cmd("find *.txt", FILER)
                assert b"fixture" in alice.cmd("info 1", FILER)
                assert b"File selected" in alice.cmd("add 1", FILER)
                if os.name == "posix":
                    transferred = alice.cmd("get 1", FILER)
                    assert b"HELPER=send:1:alice" in transferred and b"Transfer complete" in transferred
                assert b"Protocol: xmodem-1k" in alice.cmd("protocol xmodem-1k", FILER)
                assert b"one file" in alice.cmd("batch", FILER)
                assert b"requires ZMODEM" in alice.cmd("resume", FILER)
                alice.cmd("protocol zmodem", FILER)
                alice.cmd(".", MAIN)

                alice.cmd("chat", CHAT)
                bob.cmd("c" if profile=="mash" else "4", CHAT)
                assert b"hello room" in alice.cmd("hello room", CHAT)
                assert b"alice: hello room" in bob.until(CHAT)
                # Public and private messages share a per-connection rate limit.
                # Use the other sender and require a receipt, not the Telnet echo.
                online = bob.cmd("/who", CHAT)
                match = re.search(rb"(\d+)  alice", online)
                assert match, online
                sent = bob.cmd(b"/wire " + match[1] + b" hidden note", CHAT)
                assert b"[to " + match[1] + b" alice] hidden note" in sent, sent
                received = alice.until(CHAT)
                assert re.search(rb"\[wire \d+ bob\] hidden note", received), received
                alice.cmd("/leave", MAIN)
                bob.cmd("/leave", MAIN)

                alice.cmd("term", TERM)
                assert b"Profile saved" in alice.cmd("set handle Alice-synth", TERM)
                assert b"Alice-synth" in alice.cmd("profile", TERM)
                assert b"Alice-synth" in alice.cmd("findfield handle Alice-synth", TERM)
                assert b"Profile saved" in alice.cmd("set interests PrivateHobby", TERM)
                assert b"Profile saved" in alice.cmd("privacy interests private", TERM)
                bob.cmd("term", TERM)
                assert b"PrivateHobby" not in bob.cmd("profile alice", TERM)
                assert b"No matching profiles" in bob.cmd("find PrivateHobby", TERM)
                bob.cmd(".", MAIN)
                alice.cmd("lang ja", TERM)
                alice.cmd("size 100 30", TERM)
                assert b"LANG ja  SIZE 100 30" in alice.cmd("save", TERM)
                alice.cmd(".", MAIN)

                if profile=="mash":
                    bob.cmd("6", b"OTHERS>> ")
                listing = bob.cmd("4" if profile=="mash" else "6", GAMES)
                assert b"Fixture game" in listing and b"Restricted" not in listing
                if os.name == "posix" and os.geteuid() != 0:
                    assert b"Unknown game number" in bob.cmd("99", GAMES)
                    assert b"Game access denied" in bob.cmd("2", GAMES)
                    assert b"GAME_USER=2:bob" in bob.cmd("1", b"GAME_USER=2:bob")
                    assert b"ANSWER=hello" in bob.cmd("hello", GAMES)
                bob.cmd(".", MAIN)
                bob.cmd("open #1", b"INDEX> ")
                bob.send(b"w")
                bob.until(b"Title> ")
                post(bob, b"Sequencer fixture", b"note from bob", b"INDEX> ")
                bob.send(b"q")
                bob.until(NOTES)
                bob.cmd("/", MAIN)

                streamed = bob.cmd("new -n", MAIN)
                assert b"Sequencer fixture" in streamed and b"note from bob" in streamed
                bob.send(b"off -n\r\n")
                disconnected(bob)
                assert checkpoints() == []

                assert b"Sequencer fixture" in alice.cmd("new", NEW)
                assert b"note from bob" in alice.cmd("1", NEW)
                alice.cmd(".", MAIN)
                alice.send(b"off\r\n")
                disconnected(alice)
                saved = checkpoints()
                assert len(saved) == 1 and saved[0][:2] == (1, 3) and saved[0][2] > 0
        with closing(start()) as server, closing(Client(server.port)) as alice:
            login(alice, "alice")
            assert f"Sequencer: {saved[0][2]}".encode() in alice.cmd("setsequencer", MAIN)
            assert b"LANG ja  SIZE 100 30" in alice.cmd("term", TERM)
            alice.cmd(".", MAIN)
            assert b"Sequencer: 0" in alice.cmd("setsequencer reset", MAIN)
            assert checkpoints() == saved
        print(f"PASS: {profile} mail drafts, filer, transfer handoff, chat, settings, Door and sequencer restart")


if __name__ == "__main__":
    run(*sys.argv[1:])
