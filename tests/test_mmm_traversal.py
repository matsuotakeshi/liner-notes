"""Personal MMM scan order, label administration, and board-safe reading."""
from contextlib import closing, ExitStack
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile

from test_board import Server
from test_mmm_services import NEW, SECRET, login
from test_mmm_ui import GATE, MAIN, NOTES, INDEX, OPEN, post
from test_telnet import Client


def raw_admin(port):
    client = Client(port)
    try:
        client.until(GATE)
        client.cmd("RAW", b"\r\nRAW\r\n")
        client.cmd("LOGIN owner", b"PASSWORD\r\n")
        client.cmd(SECRET, b"LOGGED IN owner\r\n")
        return client
    except BaseException:
        client.close()
        raise


def add_note(client, board, title, body):
    client.cmd(f"post #{board}", b"Title> ")
    post(client, title, body, NOTES)
    client.cmd("/", MAIN)


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix="ln-mmm-traversal-") as scratch:
        root = Path(scratch)
        catalog = root / "catalog"
        catalog.write_text("1|0|all|member|General\n2|0|all|member|Sound\n"
                           "3|0|all|member|Retro\n4|0|all|member|Quiet\n", encoding="utf-8")
        admins = root / "admins"
        database = root / "bbs.sqlite"

        def start():
            return Server(binary, root / "notes", members=root / "members", catalog=catalog,
                          admins=admins if admins.exists() else None, menu=True, telnet=True,
                          language="en", options=["--database", str(database), "--ui-default", "mmm",
                                                 "--ui-allow", "mmm,rtbbs"])

        with closing(start()) as server:
            for name in ("owner", "alice", "bob"):
                with closing(Client(server.port)) as client:
                    login(client, name, register=True)
        admins.write_text("1\n", encoding="ascii")

        with closing(start()) as server, ExitStack() as stack:
            admin, alice, bob = [stack.enter_context(closing(Client(server.port))) for _ in range(3)]
            for client, name in ((admin, "owner"), (alice, "alice"), (bob, "bob")):
                login(client, name)
            raw = stack.enter_context(closing(raw_admin(server.port)))
            assert b"BOARDLABEL 2 synth" in raw.cmd("BOARDLABEL 2 SYNTH", b"BOARDLABEL 2 synth\r\n")
            assert b"BOARDLABEL 3 retro.g" in admin.cmd("boardlabel #3 Retro.G", MAIN)
            assert b"unavailable" in admin.cmd("boardlabel #3 synth", MAIN)
            assert b"BOARDLABEL 3 retro.g" in admin.cmd("boardlabel #3", MAIN)
            assert b"Administrator required" in alice.cmd("boardlabel #2 unowned", MAIN)

            add_note(admin, 1, b"General item", b"general body")
            add_note(admin, 2, b"Sound item", b"sound body")
            add_note(admin, 3, b"Retro item", b"retro body")

            assert b"No new notes." in alice.cmd("open -s #4", MAIN)
            assert b"Base notes" in alice.cmd("open -x #4", INDEX)
            alice.send(b"q")
            alice.until(NOTES)
            alice.cmd("/", MAIN)
            assert b"sound body" in alice.cmd("open -s @SYNTH", OPEN)
            alice.send(b"q")
            alice.until(NOTES)
            alice.cmd("/", MAIN)
            assert b"retro body" in alice.cmd("open -x @retro.g", OPEN)
            alice.send(b"q")
            alice.until(NOTES)
            alice.cmd("/", MAIN)

            assert b"Scanlist saved." in alice.cmd("scanlist set @retro.g #2", MAIN)
            assert b"unavailable" in alice.cmd("scanlist set @retro.g #3", MAIN)
            own = alice.cmd("scanlist", MAIN)
            assert own.index(b"#3 @retro.g") < own.index(b"#2 @synth"), own
            assert b"default" in bob.cmd("scanlist", MAIN)

            first = alice.cmd("new", NEW)
            assert b"Retro item" in first and b"Sound item" not in first and b"General item" not in first, first
            second = alice.cmd("more", NEW)
            assert b"Sound item" in second and b"General item" not in second, second
            alice.cmd(".", MAIN)

            # Start another ordered traversal, then revoke the next board before
            # requesting its page. Inaccessible personal rows cannot select or
            # disclose inaccessible data, including after restarting.
            assert b"Retro item" in alice.cmd("new -n", NEW)
            raw.cmd("BOARDSET 2|0|g7|member|Private sound", b"BOARD UPDATED 2\r\n")
            denied = alice.cmd("more", NEW)
            assert b"Sound item" not in denied and b"General item" not in denied and b"No matching" in denied, denied
            alice.cmd(".", MAIN)
            raw.cmd("BOARDSET 3|0|g7|member|Private retro", b"BOARD UPDATED 3\r\n")
            assert b"unavailable" in alice.cmd("go @retro.g", MAIN)
            own = alice.cmd("scanlist", MAIN)
            assert b"No readable boards" in own and b"retro.g" not in own and b"synth" not in own, own
            empty = alice.cmd("new", NEW)
            assert b"No matching" in empty and b"General item" not in empty, empty

        with closing(sqlite3.connect(database.as_uri()+"?mode=ro", uri=True)) as db:
            assert db.execute("SELECT position,board FROM mmm_scanlist WHERE user=2 ORDER BY position").fetchall()==[(1,3),(2,2)]
            assert db.execute("SELECT label FROM mmm_board_labels ORDER BY board").fetchall()==[("synth",),("retro.g",)]

        tool = Path(__file__).resolve().parents[1]/"scripts"/"maintenance.py"
        with tempfile.TemporaryDirectory(prefix="ln-mmm-export-") as tmp:
            exported = Path(tmp)/"exported"
            command = [sys.executable, str(tool), "db-export", "--root", str(root),
                       "--database", str(database), "--board", "notes", str(exported)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stderr
            with closing(sqlite3.connect((exported/"notes.features.sqlite").as_uri()+"?mode=ro", uri=True)) as db:
                assert db.execute("SELECT board FROM mmm_scanlist WHERE user=2 ORDER BY position").fetchall()==[(3,),(2,)]
                assert db.execute("SELECT label FROM mmm_board_labels WHERE board=3").fetchone()==("retro.g",)

        with closing(start()) as server, closing(Client(server.port)) as alice:
            login(alice, "alice")
            assert b"No readable boards" in alice.cmd("scanlist", MAIN)
            assert b"Scanlist saved." in alice.cmd("scanlist -d", MAIN)
            assert b"General item" in alice.cmd("new -n", NEW)
        print("PASS: MMM labels, ordered scanlists, -s/-x, live ACL revocation, restart and DB export")


if __name__ == "__main__":
    run(sys.argv[1])
