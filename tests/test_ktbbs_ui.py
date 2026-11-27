"""Real Telnet: KTBBS navigation, shared identities and durable data."""
from contextlib import ExitStack, closing
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from test_board import Server
from test_members import auth, PASSWORD
from test_runtime import Client as Raw
from test_telnet import Client

ID = b"User ID > "
MAIN = b"MAIN> "
BOARD = b"Board (? list / 0 back) > "
READ = b"READ> "
ARTICLE = b"ARTICLE> "
TITLE = b"Title> "
TEXT = b"Text (// ends) > "
EDIT = b"EDIT> "
MAIL = b"MAIL> "
PREF = b"UI preference saved."


def key(client, value, marker):
    client.send(value)
    return client.until(marker)


def write(client, title, body, stop):
    client.cmd(title, b"Use this title? Y / N / 0 > ")
    client.cmd("Y", TEXT)
    client.cmd(body, TEXT)
    client.cmd("//", EDIT)
    assert b"UI preference saved." not in client.cmd("UI mmm", EDIT)
    assert body.encode() in client.cmd("L", EDIT)
    return client.cmd("S", stop)


def run(binary, vaultctl):
    binary, vaultctl = (str(Path(item).resolve()) for item in (binary, vaultctl))
    with tempfile.TemporaryDirectory(prefix="ln-ktbbs-") as tmp, ExitStack() as stack:
        path = Path(tmp)
        database = ["--database-structured", str(path / "data.sqlite")]
        catalog = path / "catalog"
        catalog.write_text("1|0|all|member|Lobby\n2|0|g1|g1|Restricted board\n")
        game = path / "game"
        game.write_text(f"#!{sys.executable}\nimport os\nprint('DOOR USER='+os.environ['LINER_USER'],flush=True)\ninput('GAME> ')\n")
        game.chmod(0o700)
        doors = path / "doors"
        doors.write_text(f"1|0|30|{game}|{path}|KTBBS Door fixture\n")
        vault, data = path / "vault", path / "payload"
        data.write_bytes(b"KTBBS data\n")
        subprocess.run([vaultctl, "init", vault], check=True, capture_output=True)
        subprocess.run([vaultctl, "import", vault, data, "Fixture.txt", "test fixture"], check=True, capture_output=True)
        helper = path / "transfer-helper"
        helper.write_text('#!/bin/sh\nprintf "HELPER %s %s\\n" "$1" "$2"\nprintf "DONE OK 1 11\\n" >&3\n')
        helper.chmod(0o700)
        notes, members = path / "notes", path / "members"
        with closing(Server(binary, notes, members=members, catalog=catalog, options=database)) as seed:
            for name in ("alice", "bob"):
                with closing(Raw(seed.port)) as raw:
                    auth(raw, name, True)
        admins = path / "admins"
        admins.write_text("1\n")
        server = stack.enter_context(closing(Server(
            binary, notes, members=members, catalog=catalog, telnet=True, menu=True,
            language="en", admins=admins, vault=vault, zhelper=helper, options=database + ["--doors", str(doors), "--ui-default", "ktbbs",
                                                "--ui-allow", "rtbbs,mmm,mash,ktbbs", "--ui-policy", "MEMBERS"],
        )))
        def login(name):
            client = stack.enter_context(closing(Client(server.port)))
            greeting = client.until(ID)
            assert b"KTBBS mode" in greeting, greeting
            assert b"unavailable" in client.cmd("UI rtbbs", ID)
            client.cmd(name, b"Password> ")
            response = client.cmd(PASSWORD, MAIN)
            assert b"KTBBS | R Read" in response and PASSWORD.encode() not in response, response
            return client

        alice, bob = login("alice"), login("bob")
        alice.cmd("E", b"ENTER> ")
        alice.cmd("W", BOARD)
        alice.cmd("1", TITLE)
        saved = write(alice, "KTBBS original", "Body through KTBBS", READ)
        post_id = int(re.search(rb"Saved #(\d+)", saved)[1])
        assert b"Visible articles: 1" in alice.cmd("#", READ)
        alice.cmd("L", b"Start article # (Enter: newest) > ")
        index = alice.cmd("", b"LIST> ")
        assert b"1  #" + str(post_id).encode() in index and b"KTBBS original" in index
        assert b"Body through KTBBS" in alice.cmd("1", ARTICLE)
        key(alice, b"0", READ)
        alice.cmd("r", b"Start reading # (Enter: newest) > ")
        title = alice.cmd("", ARTICLE)
        assert b"KTBBS original" in title and b"Body through KTBBS" not in title
        assert b"Body through KTBBS" in key(alice, b"\r\n", ARTICLE)
        key(alice, b"0", READ)
        alice.cmd("0", MAIN)

        bob.cmd("R", BOARD)
        boards = bob.cmd("?", BOARD)
        assert b"Lobby" in boards and b"Restricted board" not in boards
        assert b"unavailable" in bob.cmd("2", BOARD)
        bob.cmd("0", MAIN)
        bob.cmd("UI rtbbs", b"UI PREF SAVED rtbbs")
        bob.until()
        assert b"Body through KTBBS" in bob.cmd(f"READ {post_id}")
        bob.cmd("MENU")
        bob.cmd("UI mmm", PREF)
        bob.until(b"MAIN>> ")
        bob.cmd("post #1", TITLE)
        from test_mmm_ui import post
        post(bob, b"MMM original", b"Body through MMM", b"NOTES>> ")
        bob.cmd("/", b"MAIN>> ")

        alice.cmd("R 1", READ)
        alice.cmd("S", b"SEARCH [1 sender")
        # Consume the entire question before requesting the next one.
        alice.until(b"> ")
        alice.cmd("2", b"Search value > ")
        assert b"MMM original" in alice.cmd("Body through MMM", b"LIST> ")
        assert b"Body through MMM" in alice.cmd("1", ARTICLE)
        key(alice, b"r", TITLE)
        reply = write(alice, "A KTBBS reply", "Reply body", ARTICLE)
        reply_id = int(re.search(rb"Saved #(\d+)", reply)[1])
        key(alice, b"0", READ)
        assert b"Reply body" in alice.cmd(f"I {reply_id}", ARTICLE)
        assert b"Body through MMM" in key(alice, b"d", ARTICLE)
        key(alice, b"0", READ)
        alice.cmd("0", MAIN)

        alice.cmd("E", b"ENTER> ")
        alice.cmd("M bob", TITLE)
        assert b"Mail sent" in write(alice, "KTBBS mail", "Private mail body", MAIL)
        alice.cmd("0", MAIN)
        bob.cmd("mail", b"MAIL>> ")
        assert b"KTBBS mail" in bob.cmd("inbox", b"MAIL>> ")
        assert b"Private mail body" in bob.cmd("next", b"MAIL>> ")
        bob.cmd(".", b"MAIN>> ")

        alice.cmd("F", b"File board (? list / 0 back) > ")
        alice.cmd("", b"FILE> ")
        assert b"Fixture.txt" in alice.cmd("L", b"FILE> ")
        assert b"Fixture.txt" in alice.cmd("S Fixture", b"FILE> ")
        alice.cmd("Z", b"FILE> ")
        assert b"HELPER send 1" in alice.cmd("D 1", b"FILE> ")
        assert b"HELPER receive-in" in alice.cmd("U Test description", b"FILE> ")
        alice.cmd("0", b"FILE> ")
        alice.cmd("0", MAIN)

        alice.cmd("C", b"CHAT> ")
        bob.cmd("chat", b"CHAT>> ")
        alice.cmd("KTBBS message", b"CHAT> ")
        assert b"KTBBS message" in bob.until(b"CHAT>> ")
        alice.cmd("//", MAIN)
        bob.cmd(".", b"MAIN>> ")

        alice.cmd("!", b"LINER> ")
        assert b"KTBBS Door fixture" in alice.cmd("games", b"GAMES> ")
        assert b"DOOR USER=alice" in alice.cmd("1", b"GAME> ")
        assert b"KTBBS Door fixture" in alice.cmd("quit", b"GAMES> ")
        alice.cmd("0", b"LINER> ")
        alice.cmd("0", MAIN)

        alice.cmd("N 70/01/01", b"NEWS> ")
        assert b"Body through KTBBS" in alice.cmd("1", b"NEWS> ")
        alice.cmd("0", MAIN)
        alice.cmd("UI mmm", PREF)
        alice.until(b"MAIN>> ")
        alice.cmd("UI ktbbs", PREF)
        alice.until(MAIN)
        alice.close()
        restored = login("alice")
        assert b"UI ktbbs / MEMBERS" in restored.cmd("UI", MAIN)
        with closing(Client(server.port)) as raw:
            raw.until(ID)
            raw.cmd("RAW", b"\r\nRAW\r\n")
            raw.cmd("LOGIN alice", b"PASSWORD")
            raw.cmd(PASSWORD, b"LOGGED IN alice")
            assert b"HIDDEN SAVED" in raw.cmd(f"HIDE {post_id}", b"HIDDEN SAVED")
            assert b"PONG" in raw.cmd("PING", b"PONG")
        restored.cmd("R 1", READ)
        denied = restored.cmd(f"I {post_id}", READ)
        assert b"Body through KTBBS" not in denied and b"KTBBS original" not in denied, denied

    print("PASS KTBBS: Telnet login and ACL, board numbering, article/reply, shared mail, chat, Door, saved UI and RAW")


if __name__ == "__main__":
    run(*sys.argv[1:])
