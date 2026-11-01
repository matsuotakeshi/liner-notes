"""FreeBSD rc.conf selects the structured format on a fresh installation."""
import os
import pathlib
import pwd
import shlex
import socket
import sqlite3
import subprocess
import sys
import tempfile
from test_telnet import Client

root = pathlib.Path(__file__).resolve().parents[1]
binary = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='liner-structured-rc-') as directory:
    base = pathlib.Path(directory)
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    settings = dict(enable='YES', home=str(base), data_dir=str(base),
                    program=str(binary), run_user=pwd.getpwuid(os.getuid()).pw_name,
                    port=str(port), language='en', menu_dir=str(root/'menus'),
                    help_dir=str(root/'help'), log_dir=str(base),
                    pidfile=str(base/'parent.pid'), child_pidfile=str(base/'child.pid'),
                    database=str(base/'liner.sqlite'), database_mode='structured')
    config = base/'service.conf'
    config.write_text(''.join('liner_notes_'+k+'='+shlex.quote(v)+'\n' for k,v in settings.items()))
    env = dict(os.environ, LINER_NOTES_RC_CONF=str(config))
    def service(action, check=True):
        return subprocess.run(['/bin/sh', str(root/'rc.d/liner_notes'), action],
                              env=env, check=check, capture_output=True, timeout=40)
    try:
        service('start')
        client = Client(port)
        try:
            client.until()
            client.cmd('REGISTER sqlrc', b'Password (8-128 bytes)> ')
            assert b'Logged in as sqlrc' in client.cmd('test-rc-secret-2468')
            client.cmd('POST RC article', b'Body > ')
            client.cmd('Native row from rc.conf', b'Body > ')
            assert b'Saved article #1' in client.cmd('.')
        finally:
            client.close()
        service('stop')
        with sqlite3.connect(base/'liner.sqlite') as db:
            assert db.execute('select body from article_records where record_id=1').fetchone()[0] == 'Native row from rc.conf\r\n'
        service('start')
        service('status')
        service('stop')
        print('PASS rc.conf structured selection, member/article SQL persistence, clean restart and stop')
    finally:
        service('stop', False)
