"""Public RTBBS UI selection, policy and restart persistence over real sockets."""
from contextlib import closing
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
from test_board import Server,post
from test_runtime import Client
from test_members import auth
from test_mail import send
from test_stage58 import cmd
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
        for options in (['--ui-policy', 'OTHER'], ['--ui-default', 'mmm'],
                        ['--ui-allow', 'rtbbs,mmm'], ['--ui-allow', 'rtbbs,rtbbs']):
            result = subprocess.run([binary, *options, '0'], capture_output=True, timeout=10)
            assert result.returncode == 2 and b'invalid UI' in result.stderr, result
    print('PASS public UI policy, own preference/reset, guest choice and restart fallback')


if __name__ == '__main__':
    run(sys.argv[1])
