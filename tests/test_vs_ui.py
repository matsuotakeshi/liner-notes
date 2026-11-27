"""VS Telnet acceptance for direct writes, traversal and shared replies."""
from contextlib import ExitStack, closing
from pathlib import Path
import re
import sys
import tempfile
import time

from test_board import Server
from test_members import PASSWORD, auth
from test_mmm_ui import post as mmm_post
from test_runtime import Client as Raw
from test_telnet import Client

MAIN = b"VS> "
BOARD = b"Message> "
READ = b"Read> "
NEWS = b"News> "
POINTER = b"Pointer> "
OPTIONS = ["--ui-default", "vs", "--ui-policy", "MEMBERS",
           "--ui-allow", "rtbbs,mmm,mash,ktbbs,big-model,wani-bbs,net-cock,vs"]


def message_ids(page):
    return [int(n) for n in re.findall(rb"(?m)^Message #([0-9]+) ", page)]


def post(client, command, number, body):
    client.cmd(command, b"Text> ")
    client.cmd(body, b"Text> ")
    assert f"Saved #{number}".encode() in client.cmd("/POST", BOARD)


def run(binary):
    with tempfile.TemporaryDirectory(prefix="ln-vs-") as tmp:
        root = Path(tmp)
        notes, members, catalog = (root / name for name in ("notes", "members", "catalog"))
        catalog.write_text("1|0|all|member|Free\n2|0|g1|g1|Restricted\n", encoding="ascii")
        db = ["--database-structured", str(root / "state.sqlite")]
        with closing(Server(binary, notes, members=members, catalog=catalog, options=db)) as seed:
            for user in ("alice", "bob"):
                with closing(Raw(seed.port)) as raw:
                    auth(raw, user, True)

        def start():
            return Server(binary, notes, members=members, catalog=catalog,
                          telnet=True, menu=True, language="en", options=db + OPTIONS)

        def login(stack, server, user):
            client = stack.enter_context(closing(Client(server.port)))
            client.until(b"VS ID> ")
            client.cmd("LOGIN " + user, b"Password> ")
            assert PASSWORD.encode() not in client.cmd(PASSWORD, MAIN)
            return client

        with ExitStack() as stack:
            server = stack.enter_context(closing(start()))
            alice = login(stack, server, "alice")
            assert b"unavailable" in alice.cmd("D;W;#2;Forbidden;Y", MAIN).lower()
            post(alice, "D;W;#1;First;Y", 1, "First body")
            post(alice, "DW#1;Second;Y", 2, "Second body")
            post(alice, "D;R;#1;1;Reply to first;Y", 3, "Reply one")
            post(alice, "D;R;#1;2;Reply to second;Y", 4, "Reply two")
            post(alice, "D;R;#1;3;Nested reply;Y", 5, "Nested body")
            assert b"Jump replies are unavailable" in alice.cmd("D;J;#1;3;Y;W;#1;Oops;Y", BOARD)
            assert re.findall(rb"(?m)^#([0-9]+) ", alice.cmd("L 1", BOARD)) == [b"1", b"2", b"3", b"4", b"5"]
            assert message_ids(alice.cmd("R 3", READ)) == [3]
            assert message_ids(alice.cmd("S", b"Related> ")) == [1]
            assert message_ids(alice.cmd("", b"Related> ")) == [3]
            assert message_ids(alice.cmd("", b"Related> ")) == [5]
            alice.cmd("Q", READ)
            alice.cmd("Q", BOARD)
            alice.cmd("Q", MAIN)
            assert b"News pointer: 0" in alice.cmd("P #", POINTER)
            alice.cmd("Q", MAIN)
            # The active timestamp second stays outside the traversal snapshot.
            time.sleep(1)
            assert message_ids(alice.cmd("ＮＡ", MAIN)) == [1, 2, 3, 4, 5]
            alice.cmd("P T", POINTER)
            alice.cmd("Q", MAIN)
            alice.cmd("U", b"User> ")
            alice.cmd("B", b"Preferences> ")
            assert b"News: related order." in alice.cmd("N RELATED", b"Preferences> ")
            alice.cmd("Q", b"User> ")
            alice.cmd("Q", MAIN)
            alice.cmd("N", b"News board> ")
            assert message_ids(alice.cmd("", NEWS)) == [1]
            assert message_ids(alice.cmd("", NEWS)) == [3]
            alice.cmd("Q", MAIN)

            bob = login(stack, server, "bob")
            bob.cmd("UI mash", b"UI preference saved.")
            bob.until(b"MAIN>> ")
            bob.cmd("open -b1 #1", b"OPEN> ")
            bob.send(b" ")
            assert b"Reply one" in bob.until(b"OPEN> ")
            bob.send(b"w")
            bob.until(b"Title> ")
            mmm_post(bob, b"MASH nested", b"Cross-UI reply", b"OPEN> ")
            time.sleep(1)

        with ExitStack() as stack:
            server = stack.enter_context(closing(start()))
            alice = login(stack, server, "alice")
            # An interrupted group may be replayed, but must not skip article 2 or 4.
            assert message_ids(alice.cmd("NA", MAIN)) == [1, 3, 5, 6, 2, 4]
            assert message_ids(alice.cmd("NA", MAIN)) == []
    print("PASS VS Telnet direct writes, flat and related reading, restart and MASH interoperation")


if __name__ == "__main__":
    run(str(Path(sys.argv[1]).resolve()))
