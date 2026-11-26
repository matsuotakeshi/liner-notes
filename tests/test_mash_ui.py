"""MASH dialect and shared state over isolated real Telnet connections."""

from contextlib import ExitStack, closing
from pathlib import Path
import re
import sys
import tempfile

from test_board import Server
from test_members import auth
from test_mmm_ui import GATE, MAIN, NOTES, OPEN, PASSWORD, key, post
from test_runtime import Client as Raw
from test_telnet import Client, disconnected


SECRET = "MASH-demo-password"


def select_notes_ui(client, name):
    # Selection draws the new menu immediately. Saving the preference is a
    # worker request, so wait for its acknowledgement and the ready prompt.
    switched = client.cmd("ui " + name, b"UI preference saved.")
    client.until(MAIN)
    return switched


def run(binary):
    with tempfile.TemporaryDirectory(prefix="ln-mash-") as tmp, ExitStack() as stack:
        root = Path(tmp)
        catalog = root / "catalog"
        catalog.write_text("1|0|all|member|Public\n2|1|g1|g1|Private title\n"
                           "3|0|all|member|Topics\n", encoding="utf-8")
        runtime = str(Path(binary).resolve())
        notes, members = root / "notes", root / "members"
        storage = ["--database-structured", str(root / "data.db")]
        # Start without an administrator list and register Alice first (ID 1).
        with closing(Server(runtime, notes, members=members, catalog=catalog, options=storage)) as seed:
            for name in ("alice", "bob"):
                with closing(Raw(seed.port)) as client:
                    auth(client, name, create=True, password=SECRET)

        admins = root / "admins"
        admins.write_text("1\n", encoding="ascii")
        server = stack.enter_context(closing(Server(
            runtime, notes, members=members, admins=admins, catalog=catalog,
            telnet=True, menu=True, language="en",
            options=storage + ["--ui-default", "mash", "--ui-allow", "rtbbs,mmm,mash",
                               "--ui-policy", "MEMBERS"],
        )))

        def connect(name):
            client = stack.enter_context(closing(Client(server.port)))
            greeting = client.until(GATE)
            connection = re.search(rb"RTBBS-PROBE (\d+)", greeting)[1]
            assert b"Login required" in client.cmd("bbs", GATE)
            assert b"unavailable" in client.cmd("ui mmm", GATE)
            client.cmd("login " + name, PASSWORD)
            assert b"MASH | 1 Mail" in client.cmd(SECRET, MAIN)
            return client, connection

        mash, _ = connect("alice")
        peer, peer_id = connect("bob")
        assert b"UI mash / MEMBERS / DEFAULT mash" in mash.cmd("ui", MAIN)
        assert b"readnews [-n]" in mash.cmd("READNEWS -?", MAIN)
        assert b"cmdlst" in mash.cmd("9", MAIN)
        assert b"whoami" in mash.cmd("cmdlst", MAIN)
        assert b"site-local" in mash.cmd("11", MAIN)
        assert b"Liner notes" in mash.cmd("v", MAIN)
        assert b"alice" in mash.cmd("8", MAIN)
        assert b"Profiles" in mash.cmd("4", b"TERM>> ")
        mash.cmd(".", MAIN)
        assert b"LANG" in mash.cmd("3", b"TERM>> ")
        mash.cmd(".", MAIN)
        mash.cmd("1", b"MAIL>> ")
        mash.cmd(".", MAIN)

        assert b"Connections" in mash.cmd("6", b"OTHERS>> ")
        assert b"bob" in mash.cmd("1", b"OTHERS>> ")
        receipt = mash.cmd(b"! " + peer_id + b" Case Preserved", b"OTHERS>> ")
        assert b"[to " + peer_id + b" bob] Case Preserved" in receipt, receipt
        assert b"Case Preserved" in peer.until(MAIN)
        assert b"[to " not in mash.cmd("! * forbidden", b"OTHERS>> ")
        assert b"SYSOP notified" in peer.cmd("$ Need assistance", MAIN)
        assert b"Need assistance" in mash.until(b"OTHERS>> ")
        mash.cmd(".", MAIN)

        assert b"Public" in mash.cmd("10", NOTES)
        mash.cmd("1", NOTES)
        assert b"Private title" not in mash.cmd("3", NOTES)
        assert b"unavailable" in peer.cmd("go #2", MAIN)
        mash.cmd("/", MAIN)
        assert b"unavailable" in peer.cmd("readnews", MAIN)
        assert b"BOARDLABEL 3 mash-news" in mash.cmd("boardlabel #3 mash-news", MAIN)
        mash.cmd("post #3", b"Title> ")
        post(mash, b"News fixture", b"Only in the topics board", NOTES)
        mash.cmd("/", MAIN)
        assert b"News fixture" in peer.cmd("readnews", b"NEWS> ")
        key(peer, b"q", MAIN)
        assert b"Only in the topics board" in peer.cmd("12", MAIN)

        mash.cmd("post #1", b"Title> ")
        post(mash, b"MASH base", b"MASH original", NOTES)
        mash.cmd("/", MAIN)
        assert b"3 Filer" in select_notes_ui(peer, "mmm")
        assert b"MASH original" in peer.cmd("open -b2 #1", OPEN)
        key(peer, b"w", b"Title> ")
        post(peer, b"MMM reply", b"MMM cross reply", OPEN)
        key(peer, b"q", NOTES)
        peer.cmd("/", MAIN)

        assert b"MASH original" in mash.cmd("open -b2 #1", OPEN)
        opened = key(mash, b" ", OPEN)
        assert b"MMM cross reply" in opened and b"Parent #2 (p)" in opened, opened
        key(mash, b"w", b"Title> ")
        post(mash, b"MASH nested", b"Reply to a reply", OPEN)
        assert b"Parent #3 (p)" in key(mash, b" ", OPEN)
        assert b"MMM cross reply" in key(mash, b"p", OPEN)
        assert b"MASH original" in key(mash, b"p", OPEN)
        assert b"personal file" in key(mash, b"s", OPEN)
        key(mash, b"q", NOTES)
        mash.cmd("/", MAIN)
        assert b"MASH original" in mash.cmd("pfile -t", MAIN)

        mash.cmd("talk", b"TALK) ")
        mash.cmd("1", b"TALK> ")
        assert b"Posted #1" in mash.cmd("MASH conference fixture", b"TALK> ")
        mash.cmd("/q", MAIN)
        assert b"MASH conference fixture" in peer.cmd("talk -n", b"TALK) ")
        peer.cmd("a", b"TALK) ")
        peer.cmd("q", MAIN)
        select_notes_ui(peer, "mash")
        assert b"No conference lines" in peer.cmd("14", b"TALK) ")
        peer.cmd("q", MAIN)

        select_notes_ui(mash, "mmm")
        assert b"MASH original" not in mash.cmd("pfile -t", MAIN)
        select_notes_ui(mash, "mash")
        assert b"MASH original" in mash.cmd("pfile -t", MAIN)
        mash.cmd("ui rtbbs", b"UI PREF SAVED rtbbs")
        mash.until()
        assert b"Reply to a reply" in mash.cmd("READ 4")
        select_notes_ui(mash, "mash")
        assert b"Sequencer: 0 (session)" in mash.cmd("sets reset", MAIN)
        mash.send(b"bye\r\n")
        disconnected(mash)
        again, _ = connect("alice")
        assert b"MASH original" not in again.cmd("pfile -t", MAIN)
        print("PASS: MASH numbering, command usage, ACL, telegram, news, PLINK, shared MMM state and UI isolation")


if __name__ == "__main__":
    run(sys.argv[1])

