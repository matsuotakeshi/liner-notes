"""Public RTBBS UI selection, policy and restart persistence over real sockets."""
from contextlib import closing
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
from test_board import Server,post
from test_runtime import Client
from test_telnet import Client as Terminal
from test_members import PASSWORD
from test_members import auth
from test_mail import send
from test_stage58 import cmd, rows as raw_rows
from test_ui import Menu


def run(binary):
    binary = str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='ui-selection-') as tmp:
        root = Path(tmp)
        database = root/'notes.features.sqlite'
        def start(policy='MEMBERS'):
            return Server(binary, root/'notes', members=root/'members', language='en',
                          options=['--ui-policy', policy, '--ui-default', 'rtbbs', '--ui-allow', 'rtbbs'])
        def rows():
            with closing(sqlite3.connect(database)) as db:
                return db.execute('SELECT user,profile FROM ui_preferences ORDER BY user').fetchall()
        with closing(start()) as server:
            with closing(Client(server.port)) as alice, closing(Client(server.port)) as guest:
                assert b'not permitted' in cmd(guest, 'UI rtbbs')
                auth(alice, 'alice', True)
                assert cmd(alice, 'UI rtbbs') == b'UI PREF SAVED rtbbs'
                assert rows() == [(1, 'rtbbs')]
                assert b'not permitted' in cmd(alice, 'UI mmm')
                assert rows() == [(1, 'rtbbs')]
                assert cmd(alice, 'UI RESET') == b'UI PREF SAVED DEFAULT'
                assert rows() == []
                # Sending a message leaves the client in mail. A human-readable
                # busy response is not a successful preference save.
                send(alice, 'alice', 'selection boundary', 'body')
                assert cmd(alice, 'UI RTBBS') == b'Finish the current operation before changing UI.'
                assert rows() == []
                assert cmd(alice, '0') == b'MAIL CLOSED'
                assert cmd(alice, 'UI RTBBS') == b'UI PREF SAVED rtbbs'
                assert rows() == [(1, 'rtbbs')]
                # A selected article keeps the board context across mail.
                # Closing mail must not be mistaken for returning to main.
                post(alice, 'board selection', 'body')
                send(alice, 'alice', 'nested mail', 'body')
                assert cmd(alice, '0') == b'MAIL CLOSED'
                assert cmd(alice, 'UI RESET') == b'Finish the current operation before changing UI.'
                assert rows() == [(1, 'rtbbs')]
        with closing(start('FIXED')) as server:
            with closing(Client(server.port)) as alice:
                auth(alice, 'alice')
                alice.send(b'UI\n')
                assert alice.line() == b'UI rtbbs / FIXED / DEFAULT rtbbs'
                assert alice.line() == b'  UI rtbbs'
                assert rows() == [(1, 'rtbbs')]
        # A previously installed profile remains stored when disabled/uninstalled.
        with closing(sqlite3.connect(database)) as db:
            db.execute("UPDATE ui_preferences SET profile='future-ui'"); db.commit()
        with closing(start('ALL')) as server:
            with closing(Menu(server.port)) as client:
                assert 'this connection' in cmd(client, 'UI rtbbs').decode()
                auth(client, 'alice')
                assert rows() == [(1, 'future-ui')]
                client.cmd('MENU')
                assert 'ALL' in client.cmd('UI')
                assert 'UI PREF SAVED DEFAULT' in client.cmd('UI RESET')
                assert rows() == []
        # A single Telnet connection visits every registered profile. The
        # selector uses the same configured ordering in every adapter.
        profiles = ("rtbbs", "mmm", "mash", "ktbbs", "big-model", "wani-bbs", "net-cock", "vs")
        prompts = (b"] > ", b"MAIN>> ", b"MAIN>> ", b"MAIN> ", b"BIG> ", b"/> ", b"Menu> ", b"VS> ")
        options = ["--ui-policy", "MEMBERS", "--ui-allow", ",".join(profiles)]
        with closing(Server(binary, root/"notes", members=root/"members", language="en",
                            telnet=True, menu=True, options=options)) as server:
            with closing(Terminal(server.port)) as client:
                client.until(prompts[0])
                client.cmd("LOGIN alice", b"> ")
                client.cmd(PASSWORD, prompts[0])
                assert b"Terminal UI selection" in client.cmd("MENU", prompts[0])
                assert b"Topic posted." in client.cmd("TOPIC -W Shared topic from RTBBS", prompts[0])
                for index, prompt in enumerate(prompts):
                    chooser = client.cmd("UI", prompt)
                    for number, name in enumerate(profiles, 1):
                        assert f"  {number}. {name}\r\n".encode() in chooser, chooser
                    client.cmd("UI CANCEL", prompt)
                    if index:
                        assert b"LN SIGNATURE" in client.cmd("LN", prompt)
                    if index==1:
                        assert b"Shared topic from RTBBS" in client.cmd("TOPIC ALL", prompt)
                    if index+1<len(profiles):
                        client.cmd(f"UI {index+2}", b"UI preference saved.")
                        client.until(prompts[index+1])
                client.cmd("UI RESET", b"UI PREF SAVED DEFAULT")
                client.until(prompts[0])
                assert rows()==[]
        for options in (['--ui-policy', 'OTHER'], ['--ui-default', 'mmm'],
                        ['--ui-allow', 'rtbbs,unknown'], ['--ui-allow', 'rtbbs,rtbbs']):
            result = subprocess.run([binary, *options, '0'], capture_output=True, timeout=10)
            assert result.returncode == 2 and b'invalid UI' in result.stderr, result
    flat_board(binary)
    print('PASS public UI policy, own preference/reset, guest choice and restart fallback')


def flat_board(binary):
    with tempfile.TemporaryDirectory(prefix="ui-flat-board-") as tmp:
        root = Path(tmp)
        catalog = root/"catalog"
        catalog.write_text("1|0|all|member|Main\n2|0|all|member|Other\n", encoding="ascii")
        with closing(Server(binary, root/"notes", members=root/"members", catalog=catalog,
                            language="en")) as server:
            with closing(Client(server.port)) as raw:
                auth(raw, "alice", True)
                assert cmd(raw, "BOARD 1").startswith(b"BOARD")
                for i, parent in enumerate((None, None, 1, None, 2, 3), 1):
                    assert post(raw, f"Flat row {i}", f"Marker for article {i}", parent)==i
                original = b"\n".join(raw_rows(raw, "NOTES"))
                assert b"Flat row 4" in original and b"Flat row 3" not in original
                cmd(raw, "BOARD 2")
                assert post(raw, "Other board only", "Keep the selected board") == 7
        options = ["--ui-policy", "MEMBERS", "--ui-allow", "rtbbs,ktbbs"]
        with closing(Server(binary, root/"notes", members=root/"members", catalog=catalog,
                            language="en", menu=True, telnet=True, options=options)) as server:
            with closing(Terminal(server.port)) as client:
                prompt=b"] > "
                client.until(prompt)
                client.cmd("LOGIN alice", b"> ")
                client.cmd(PASSWORD, prompt)
                listing=client.cmd("BOARD 1", prompt)
                assert all(f"Flat row {i}".encode() in listing for i in range(1,6)), listing
                assert b"Flat row 6" in client.cmd("+", prompt)
                assert b"Marker for article 2" in client.cmd("READ 2", prompt)
                assert b"Marker for article 3" in client.cmd("NEXT", prompt)
                assert b"Marker for article 2" in client.cmd("PREV", prompt)
                client.cmd("MENU", prompt)
                client.cmd("UI 2", b"UI preference saved.")
                client.until(b"MAIN> ")
                client.cmd("R 1", b"READ> ")
                client.cmd("L", b"Start article # (Enter: newest) > ")
                listing=client.cmd("1", b"LIST> ")
                assert all(f"Flat row {i}".encode() in listing for i in range(1,7)), listing
                assert b"unavailable" in client.cmd("#7", b"LIST> ")
    print("PASS RTBBS and KTBBS flat order, board scope and RAW NOTES compatibility")


if __name__ == '__main__':
    run(sys.argv[1])
