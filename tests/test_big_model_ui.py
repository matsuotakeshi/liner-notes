"""BIG-Model flat views over the same SQLite data as the two HyperNotes profiles."""
from contextlib import ExitStack, closing
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from test_board import Server
from test_members import PASSWORD, auth
from test_mmm_ui import post
from test_runtime import Client as Raw
from test_telnet import Client

ID = b"ID > "
MAIN = b"BIG> "
BOARD = b"BOARD> "
INDEX = b"LIST> "
ARTICLE = b"ARTICLE> "
PROFILES = ["--ui-default", "big-model", "--ui-policy", "MEMBERS",
            "--ui-allow", "rtbbs,mmm,mash,ktbbs,big-model"]


def select(client, name, prompt):
    client.cmd(f"UI {name}", b"UI preference saved.")
    client.until(prompt)


def ids(page):
    return [int(row) for row in re.findall(rb"(?m)^([0-9]{5,})  ", page)]


def run(binary, vaultctl, dbconvert):
    binary, vaultctl, dbconvert = (str(Path(item).resolve()) for item in (binary, vaultctl, dbconvert))
    with tempfile.TemporaryDirectory(prefix="ln-big-model-") as tmp:
        root = Path(tmp).resolve()
        notes, members = root / "notes", root / "members"
        catalog, admins = root / "catalog", root / "admins"
        catalog.write_text("1|0|all|member|Lobby\n2|1|all|member|Workshop\n"
                           "3|1|g1|g1|Private SIG\n", encoding="utf8")
        db = root / "data.sqlite"
        database = ["--database-structured", str(db)]
        vault, file = root / "vault", root / "payload"
        file.write_bytes(b"BIG Model fixture\n")
        subprocess.run([vaultctl, "init", vault], check=True, capture_output=True)
        subprocess.run([vaultctl, "import", vault, file, "BIG.TXT", "fixture"], check=True, capture_output=True)
        legacy = root / "legacy"
        (legacy / "vault").mkdir(parents=True)
        (vault / "index.dat").rename(legacy / "vault" / "index.dat")
        subprocess.run([dbconvert, db, legacy], input="vault/index.dat\n", text=True, check=True)
        helper = root / "transfer-helper"
        helper.write_text('#!/bin/sh\nprintf "HELPER %s %s\\n" "$1" "$2"\nprintf "DONE OK 1 18\\n" >&3\n')
        helper.chmod(0o700)
        game, doors = root / "game", root / "doors"
        game.write_text("#!/bin/sh\nprintf 'GAME USER=%s\\nGAME> ' \"$LINER_USER\"\nread -r answer\n")
        game.chmod(0o700)
        doors.write_text(f"1|0|30|{game}|{root}|BIG Model Door fixture\n")
        with closing(Server(binary, notes, members=members, catalog=catalog, options=database)) as seed:
            for name in ("alice", "bob"):
                with closing(Raw(seed.port)) as raw:
                    auth(raw, name, True)
        admins.write_text("1\n", encoding="ascii")

        def start():
            return Server(binary, notes, members=members, catalog=catalog, admins=admins,
                          telnet=True, menu=True, language="en", vault=vault, zhelper=helper,
                          options=database + PROFILES + ["--doors", str(doors)])

        def login(stack, server, name):
            client = stack.enter_context(closing(Client(server.port)))
            client.until(ID)
            assert b"UI selection unavailable" in client.cmd("UI mmm", ID)
            client.cmd(name, b"Password> ")
            assert PASSWORD.encode() not in client.cmd(PASSWORD, MAIN)
            return client

        with ExitStack() as stack:
            server = stack.enter_context(closing(start()))
            alice, bob = (login(stack, server, name) for name in ("alice", "bob"))
            sig = alice.cmd("b", b"SIG> ")
            assert b"Lobby" in sig and b"Private SIG" not in sig
            alice.cmd("1", BOARD)
            bob.cmd("B", b"SIG> ")
            bob.cmd("1", BOARD)
            children = bob.cmd("B", b"SIG> ")
            assert b"Workshop" in children and b"Private SIG" not in children
            bob.cmd("/", MAIN)
            alice.cmd("B", b"SIG> ")
            alice.cmd("1", BOARD)
            alice.cmd("0", b"SIG> ")
            alice.cmd("0", b"SIG> ")
            alice.cmd("1", BOARD)
            alice.cmd("W", b"Title> ")
            post(alice, b"Big root", b"Article one", BOARD)

            select(bob, "mmm", b"MAIN>> ")
            bob.cmd("post #1", b"Title> ")
            post(bob, b"MMM root", b"Article two", b"NOTES>> ")
            bob.cmd("/", b"MAIN>> ")
            assert ids(alice.cmd("R", INDEX)) == [1, 2]
            assert b"Article one" in alice.cmd("R 00001", ARTICLE)
            alice.cmd("A", b"Title> ")
            post(alice, b"Big reply", b"Reply to one", ARTICLE)
            alice.cmd("0", INDEX)

            select(bob, "mash", b"MAIN>> ")
            bob.cmd("open -b2 #1", b"OPEN> ")
            bob.send(b"w")
            bob.until(b"Title> ")
            post(bob, b"MASH reply", b"Reply to two", b"OPEN> ")
            bob.send(b"q")
            bob.until(b"NOTES>> ")
            bob.cmd("/", b"MAIN>> ")

            assert b"Reply to one" in alice.cmd("3", ARTICLE)
            alice.cmd("A", b"Title> ")
            post(alice, b"Big nested", b"Reply to three", ARTICLE)
            assert b"Reply to two" in alice.cmd("N", ARTICLE)
            assert b"Reply to one" in alice.cmd("P", ARTICLE)
            assert b"Reply to three" in alice.cmd("R 5", ARTICLE)
            assert b"Reply to one" in alice.cmd("G", ARTICLE)
            alice.cmd("L", INDEX)
            assert ids(alice.cmd("R", INDEX)) == [1, 2, 3, 4, 5]
            assert ids(alice.cmd("S Reply to", INDEX)) == [3, 4, 5]
            indexed = bob.cmd("open -i #1", b"INDEX> ")
            assert b"Big root" in indexed and b"MMM root" in indexed, indexed
            assert b"Big reply" not in indexed and b"MASH reply" not in indexed, indexed
            bob.send(b"q")
            bob.until(b"NOTES>> ")
            bob.cmd("/", b"MAIN>> ")
            opened = bob.cmd("open -b1 #1", b"OPEN> ")
            assert b"Article one" in opened
            bob.send(b" ")
            assert b"Reply to one" in bob.until(b"OPEN> ")
            bob.send(b" ")
            nested = bob.until(b"OPEN> ")
            assert b"Reply to three" in nested and b"Parent #3" in nested, nested
            bob.send(b"q")
            bob.until(b"NOTES>> ")
            bob.cmd("/", b"MAIN>> ")

            alice.cmd("/", MAIN)
            alice.cmd("M", b"MAIL> ")
            alice.cmd("W bob", b"Title> ")
            post_mail(alice)
            bob.cmd("mail", b"MAIL>> ")
            assert b"Big mail" in bob.cmd("inbox", b"MAIL>> ")
            assert b"Private body" in bob.cmd("next", b"MAIL>> ")
            bob.cmd(".", b"MAIN>> ")
            alice.cmd("0", MAIN)
            alice.cmd("P", b"FILES> ")
            assert b"BIG.TXT" in alice.cmd("L", b"FILES> ")
            assert b"BIG.TXT" in alice.cmd("S *.TXT", b"FILES> ")
            assert b"HELPER send 1" in alice.cmd("D 1", b"FILES> ")
            alice.cmd("0", MAIN)
            alice.cmd("+", b"LINER> ")
            assert b"BIG Model Door fixture" in alice.cmd("games", b"GAMES> ")
            assert b"GAME USER=alice" in alice.cmd("1", b"GAME> ")
            assert b"BIG Model Door fixture" in alice.cmd("quit", b"GAMES> ")
            alice.cmd("0", b"LINER> ")
            alice.cmd("0", MAIN)
            task = alice.cmd("T", b"Task number (0 cancels) > ")
            index = re.search(rb"\[(\d+)\] bob", task)[1]
            alice.cmd(index, b"Telegram (21 characters) > ")
            assert b"1\xe2\x80\x9321" in alice.cmd("x" * 22, b"Telegram (21 characters) > ")
            assert b"private hello" in alice.cmd("private hello", MAIN)
            assert b"private hello" in bob.until(b"MAIN>> ")

            alice.cmd("U", b"SETUP> ")
            assert b"saved" in alice.cmd("6 #2 #1", b"SETUP> ")
            saved = alice.cmd("7", b"SETUP> ")
            assert saved.index(b"Workshop") < saved.index(b"Lobby"), saved
            alice.cmd("5", b"Date (YYYY-MM-DD or RESET) > ")
            alice.cmd("1970-01-02", b"SETUP> ")
            alice.cmd("0", MAIN)
            assert ids(alice.cmd("O", b"NEWS> ")) == [1, 2, 3, 4, 5]
            alice.cmd("Q", MAIN)
            assert ids(alice.cmd("N resume", b"NEWS> ")) == [1, 2, 3, 4, 5]
            alice.cmd("0", MAIN)

            with closing(Client(server.port)) as raw:
                raw.until(ID)
                raw.cmd("RAW", b"\r\nRAW\r\n")
                raw.cmd("LOGIN alice", b"PASSWORD")
                raw.cmd(PASSWORD, b"LOGGED IN alice")
                raw.cmd("HIDE 1", b"HIDDEN SAVED")
            alice.cmd("B", b"SIG> ")
            alice.cmd("1", BOARD)
            assert ids(alice.cmd("R", INDEX)) == [2, 4]

        with ExitStack() as stack:
            server = stack.enter_context(closing(start()))
            alice = login(stack, server, "alice")
            alice.cmd("U", b"SETUP> ")
            assert b"#2  Workshop" in alice.cmd("7", b"SETUP> ")

    print("PASS BIG-Model: flat/HyperNotes roundtrip, hierarchy and ACL, private mail and telegram, exploration and restart")


def post_mail(client):
    client.cmd("Big mail", b"Text> ")
    client.cmd("Private body", b"Text> ")
    client.cmd(".", b"Editor>> ")
    assert b"Mail sent" in client.cmd("w", b"MAIL> ")


if __name__ == "__main__":
    run(*sys.argv[1:])
