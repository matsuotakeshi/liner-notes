"""Real Telnet clients share flat and HyperNotes views through the board worker."""
from contextlib import ExitStack, closing
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from test_board import Server
from test_members import PASSWORD, auth
from test_mmm_ui import post as mmm_post
from test_runtime import Client as Raw
from test_telnet import Client

MENU = b"Menu> "
COMMAND = b"> "
READ = b"Read> "
WRITE = b"Write (Y/N/T/E/C/Q)> "
PROFILE = ["--ui-default", "net-cock", "--ui-policy", "MEMBERS",
           "--ui-allow", "rtbbs,mmm,mash,ktbbs,big-model,wani-bbs,net-cock"]


def post(client, command, title, body, prompt=READ, confirmation="Y"):
    client.cmd(command, b"Title> ")
    client.cmd(title, b"Text> ")
    client.cmd(body, b"Text> ")
    client.cmd(".", WRITE)
    return client.cmd(confirmation, prompt)


def switch(client, profile, prompt):
    client.cmd("UI " + profile, b"UI preference saved.")
    client.until(prompt)


def ids(page):
    return [int(n) for n in re.findall(rb"(?m)^#([0-9]+)  ", page)]


def run(binary, vaultctl, dbconvert):
    binary, vaultctl, dbconvert = (str(Path(p).resolve()) for p in (binary, vaultctl, dbconvert))
    with tempfile.TemporaryDirectory(prefix="ln-net-cock-") as tmp:
        root = Path(tmp)
        notes, members, catalog, admins = (root / name for name in ("notes", "members", "catalog", "admins"))
        catalog.write_text("1|0|all|member|Free\n2|1|all|member|Lab\n3|1|g1|g1|Restricted\n", encoding="utf8")
        db = ["--database-structured", str(root / "state.sqlite")]
        vault, payload = root / "vault", root / "payload"
        payload.write_bytes(b"File body\n")
        subprocess.run([vaultctl, "init", vault], check=True, capture_output=True)
        subprocess.run([vaultctl, "import", vault, payload, "DEMO.TXT", "PDS example"], check=True, capture_output=True)
        legacy = root / "legacy"
        (legacy / "vault").mkdir(parents=True)
        (vault / "index.dat").rename(legacy / "vault" / "index.dat")
        subprocess.run([dbconvert, db[1], legacy], input="vault/index.dat\n", text=True, check=True)
        def start():
            return Server(binary, notes, members=members, catalog=catalog, admins=admins, vault=vault,
                          telnet=True, menu=True, language="en", options=db + PROFILE + ["--doors", str(doors)])

        with closing(Server(binary, notes, members=members, catalog=catalog, options=db)) as seed:
            for user in ("alice", "bob"):
                with closing(Raw(seed.port)) as raw:
                    auth(raw, user, True)
        admins.write_text("1\n", encoding="ascii")
        game, doors = root / "game", root / "doors"
        game.write_text("#!/bin/sh\nprintf 'NET-COCK door: %s\\n' \"$LINER_USER\"\n")
        game.chmod(0o700)
        doors.write_text(f"1|0|30|{game}|{root}|Door fixture\n")

        def login(stack, server, user, prompt=MENU):
            client = stack.enter_context(closing(Client(server.port)))
            client.until(b"ID> ")
            client.cmd("LOGIN " + user, b"Password> ")
            assert PASSWORD.encode() not in client.cmd(PASSWORD, prompt)
            return client

        with ExitStack() as stack:
            server = stack.enter_context(closing(start()))
            alice, bob = (login(stack, server, user) for user in ("alice", "bob"))
            assert b"1. BREAD" in alice.cmd("2", MENU)
            assert b"1/A  #1  Free" in alice.cmd("1", b"Board> ")
            assert b"Lab" in alice.cmd("A", b"Board> ")
            alice.cmd("0", b"Board> ")
            alice.cmd("1", READ)
            assert b"Saved #1" in post(alice, "W", "One", "Body one")
            assert b"Saved #2" in post(alice, "W", "Two", "Body two")
            assert ids(alice.cmd("A", READ)) == [1, 2]
            assert b"One" in alice.cmd("1", READ)
            displayed = alice.cmd("R", READ)
            assert b"Body one" in displayed and b"Two" in displayed and b"Body two" not in displayed
            assert b"Saved #3" in post(alice, "P", "Reply to two", "Parent two")
            assert b"Saved #4" in post(alice, "F", "Reply to one", "Parent one")
            assert b"Reply to #2" in alice.cmd("3", READ)
            assert b"Two" in alice.cmd("U", READ)
            assert b"Reply to two" in alice.cmd("I", READ)

            switch(bob, "mash", b"MAIN>> ")
            bob.cmd("open -b2 #1", b"OPEN> ")
            bob.send(b" ")
            assert b"Parent two" in bob.until(b"OPEN> ")
            bob.send(b"w")
            bob.until(b"Title> ")
            mmm_post(bob, b"MASH nested", b"Parent three", b"OPEN> ")
            bob.send(b"q")
            bob.until(b"NOTES>> ")
            bob.cmd("/", b"MAIN>> ")
            switch(bob, "mmm", b"MAIN>> ")
            bob.cmd("open -b1 #1", b"OPEN> ")
            bob.send(b" ")
            assert b"Parent one" in bob.until(b"OPEN> ")
            bob.send(b"q")
            bob.until(b"NOTES>> ")
            bob.cmd("/", b"MAIN>> ")
            switch(bob, "wani-bbs", b"/> ")
            bob.cmd("b #1", b"/board/1> ")
            bob.cmd("2", b"/board/1 read> ")
            assert b"Page 2  #5" in bob.cmd(".", b"/board/1 pages> ")
            bob.cmd("cd /", b"/> ")
            switch(bob, "big-model", b"BIG> ")
            bob.cmd("B", b"SIG> ")
            bob.cmd("1", b"BOARD> ")
            assert b"MASH nested" in bob.cmd("R", b"LIST> ")
            bob.cmd("/", b"BIG> ")
            switch(bob, "net-cock", MENU)
            assert b"MASH nested" in alice.cmd("5", READ)
            assert b"Reply to two" in alice.cmd("U", READ)
            assert b"MASH nested" in alice.cmd("I", READ)
            assert ids(alice.cmd("A", READ)) == [1, 2, 3, 4, 5]

            alice.cmd("Q", MENU)
            assert b"Mode saved" in alice.cmd("MODE COMMAND", COMMAND)
            alice.cmd("BSET 1 Y", COMMAND)
            alice.cmd("PMOVE T", COMMAND)
            assert b"Body one" in alice.cmd("RALL", READ)
            alice.cmd("Q", COMMAND)
            stream = alice.cmd("RNALL N", COMMAND)
            assert b"Body one" in stream and b"Parent three" in stream

            alice.cmd("MWRITE bob", b"Title> ")
            alice.cmd("Private mail", b"Text> ")
            alice.cmd("Private body", b"Text> ")
            alice.cmd(".", WRITE)
            assert b"Mail sent" in alice.cmd("Y", b"Mail> ")
            alice.cmd("Q", COMMAND)
            alice.cmd("MWRITE bob", b"Title> ")
            alice.cmd("Second mail", b"Text> ")
            alice.cmd("Second body", b"Text> ")
            alice.cmd(".", WRITE)
            assert b"Mail sent" in alice.cmd("Y", b"Mail> ")
            alice.cmd("Q", COMMAND)
            inbox = bob.cmd("MREAD", b"Mail> ")
            assert b"Private mail" in inbox and b"Second mail" in inbox, inbox
            # A mail-arrival notice can redraw the prompt. Wait for the
            # response header before treating the next prompt as completion.
            for command, number, expected in (("T", 1, b"Private body"),
                                               ("N", 2, b"Second body"),
                                               ("B", 1, b"Private body"),
                                               ("E", 2, b"Second body")):
                response = bob.cmd(command, f"Mail #{number} ".encode()) + bob.until(b"Mail> ")
                assert expected in response, response
            bob.cmd("Q", MENU)

            alice.cmd("PDS", b"PDS> ")
            assert b"DEMO.TXT" in alice.cmd("1", b"PDS> ")
            assert b"File selected" in alice.cmd("S", b"PDS> ")
            assert b"#1  pending" in alice.cmd("BATCH L", b"Batch> ")
            alice.cmd("N", b"PDS> ")
            alice.cmd("Q", COMMAND)
            alice.cmd("GAMES", b"Game> ")
            assert b"NET-COCK door: alice" in alice.cmd("1", b"Game> ")
            alice.cmd("0", COMMAND)
            assert b"Topic posted" in alice.cmd("TOPIC -W Shared topic", COMMAND)
            assert b"Shared topic" in bob.cmd("TOPIC", MENU)

            alice.cmd("B #1", READ)
            assert b"Draft saved" in post(alice, "W", "Saved title", "Saved body", confirmation="Q")
            alice.cmd("DRAFT", WRITE)
            assert b"Saved body" in alice.cmd("C", WRITE)
            assert b"Saved #6" in alice.cmd("Y", READ)

            with closing(Client(server.port)) as raw:
                raw.until(b"ID> ")
                raw.cmd("RAW", b"\r\nRAW\r\n")
                raw.cmd("LOGIN alice", b"PASSWORD")
                raw.cmd(PASSWORD, b"LOGGED IN alice")
                raw.cmd("HIDE 2", b"HIDDEN SAVED")
            listed = alice.cmd("A", READ)
            assert ids(listed) == [1, 4, 6] and b"Parent two" not in listed
            assert b"unavailable" in alice.cmd("5", READ)

        with ExitStack() as stack:
            server = stack.enter_context(closing(start()))
            alice = login(stack, server, "alice", COMMAND)
            assert b"#1  Free" in alice.cmd("BSET LIST", COMMAND)
            assert b"No more new messages" in alice.cmd("RNALL", COMMAND)
    print("PASS NET-COCK through Telnet: menus, flat/HyperNotes, P/F, U/I, Topic, Mail, PDS, Door and settings persistence")


if __name__ == "__main__":
    run(*sys.argv[1:])
