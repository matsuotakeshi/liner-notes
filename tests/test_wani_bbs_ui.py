"""WANI-BBS on the shared Telnet listener and SQLite store."""
from contextlib import ExitStack, closing
from pathlib import Path
import re
import sys
import tempfile

from test_board import Server
from test_members import PASSWORD, auth
from test_mmm_ui import post as mmm_post
from test_runtime import Client as Raw
from test_telnet import Client

MAIN = b"/> "
BOARD = b"/board/1> "
INDEX = b"/board/1 index> "
PAGES = b"/board/1 pages> "
READ = b"/board/1 read> "
EDITOR = b"Editor> "
PROFILE = ["--ui-default", "wani-bbs", "--ui-policy", "MEMBERS",
           "--ui-allow", "rtbbs,mmm,mash,ktbbs,big-model,wani-bbs"]


def post(client, title, body, prompt, command="w"):
    client.cmd(command, b"Title> ")
    client.cmd(title, b"Text> ")
    client.cmd(body, b"Text> ")
    client.cmd(".", EDITOR)
    output = client.cmd("W", prompt)
    assert b"Saved #" in output, output


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="ln-wani-bbs-") as tmp:
        root = Path(tmp)
        notes, members = root / "notes", root / "members"
        catalog, admins = root / "catalog", root / "admins"
        catalog.write_text("1|0|all|member|Workshop\n2|1|g1|g1|Private\n3|0|all|member|Library\n")
        db = ["--database-structured", str(root / "data.sqlite")]
        with closing(Server(binary, notes, members=members, catalog=catalog, options=db)) as seed:
            for name in ("alice", "bob"):
                with closing(Raw(seed.port)) as client:
                    auth(client, name, True)
        admins.write_text("1\n", encoding="ascii")
        game, doors = root / "game", root / "doors"
        game.write_text("#!/bin/sh\nprintf 'TEST DOOR: %s\\n' \"$LINER_USER\"\n")
        game.chmod(0o700)
        doors.write_text(f"1|0|30|{game}|{root}|WANI fixture\n")

        def start():
            return Server(binary, notes, members=members, catalog=catalog, admins=admins,
                          telnet=True, menu=True, language="en",
                          options=db + PROFILE + ["--doors", str(doors)])

        def login(stack, server, name):
            client = stack.enter_context(closing(Client(server.port)))
            client.until(b"Login> ")
            assert b"UI selection unavailable" in client.cmd("UI mmm", b"Login> ")
            client.cmd("login " + name, b"Password> ")
            assert PASSWORD.encode() not in client.cmd(PASSWORD, MAIN)
            return client

        with ExitStack() as stack:
            server = stack.enter_context(closing(start()))
            alice, bob = (login(stack, server, name) for name in ("alice", "bob"))
            board_list = alice.cmd("b", b"/board> ")
            assert b"Workshop" in board_list and b"Library" in board_list and b"Private" not in board_list
            alice.cmd("1", BOARD)
            assert b"Multi-notes" in alice.cmd("?", BOARD)
            post(alice, "cover", "cover body", BOARD)
            post(alice, "other cover", "without pages", BOARD)
            assert b"cover body" in alice.cmd("1", READ)
            post(alice, "first page", "first page body", READ, "W")

            bob.cmd("UI mmm", b"UI preference saved.")
            bob.until(b"MAIN>> ")
            bob.cmd("open -b1 #1", b"OPEN> ")
            bob.send(b" ")
            assert b"first page body" in bob.until(b"OPEN> ")
            bob.send(b"w")
            bob.until(b"Title> ")
            mmm_post(bob, b"nested page", b"reply to first page", b"OPEN> ")
            bob.send(b"q")
            bob.until(b"NOTES>> ")
            bob.cmd("/", b"MAIN>> ")
            bob.cmd("UI wani-bbs", b"UI preference saved.")
            bob.until(MAIN)

            alice.cmd("1", READ)
            pages = alice.cmd(".", PAGES)
            assert re.findall(rb"Page [0-9]+  #(\d+)", pages) == [b"3", b"4"], pages
            assert b"reply to first page" in alice.cmd("2", READ)
            assert b"Personal file replaced" in alice.cmd("S", READ)
            assert b"reply to first page" in alice.cmd("type", READ)
            alice.cmd("layout 1 flat", READ)
            alice.cmd("cd /", MAIN)
            alice.cmd("b Workshop", BOARD)
            page = alice.cmd("l", INDEX)
            assert re.findall(rb"Article #(\d+)", page) == [b"1", b"2", b"3", b"4"], page
            assert b"without pages" in alice.cmd("2", READ)
            assert b"first page body" in alice.cmd("", READ)
            assert b"without pages" in alice.cmd("B", READ)
            alice.cmd("b", b"/board> ")
            alice.cmd("cd /", MAIN)
            assert b"unavailable" in bob.cmd("layout 1 mnotes", MAIN)
            assert b"unavailable" in bob.cmd("b #2", MAIN)
            assert b"Topic posted" in bob.cmd("topic -w quiet message", MAIN)
            assert b"quiet message" in alice.cmd("topic", MAIN)
            assert b"No topic entries" in alice.cmd("topic", MAIN)
            assert b"quiet message" in bob.cmd("topic", MAIN)
            assert b"Topic posted" in alice.cmd("topic -w another message", MAIN)
            assert b"unavailable" in bob.cmd("topic -d 2", MAIN)
            assert b"Topic deleted" in alice.cmd("topic -d 1", MAIN)
            alice.cmd("g", b"/game> ")
            assert b"TEST DOOR: alice" in alice.cmd("1", b"/game> ")
        with ExitStack() as stack:
            server = stack.enter_context(closing(start()))
            alice = login(stack, server, "alice")
            assert b"another message" in alice.cmd("topic", MAIN)
            assert b"quiet message" not in alice.cmd("topic all", MAIN)
            assert b"0 / 10240" in alice.cmd("files", MAIN)
            alice.cmd("b #1", BOARD)
            assert b"Flat board" in alice.cmd("?", BOARD)
            alice.cmd("layout 1 mnotes", BOARD)
            assert b"Note #2" in alice.cmd("l", INDEX)
            assert b"without pages" in alice.cmd("2", READ)
    print("PASS WANI-BBS directory, flat and multi-notes, MMM interoperability, Topic, Door and restart")


if __name__ == "__main__":
    run(sys.argv[1])
