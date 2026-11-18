"""Vault mutation services through real identities, worker ACLs and both stores."""
from contextlib import closing, ExitStack
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth, PASSWORD
from test_stage58 import cmd, receive, counts
from test_vault_query_core import expect, rows, pages
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from transfer_connectors import LineConnector


def password(client, path, secret):
    expect(client, 'FILEBOARDPASS ' + path, b'NEW PASSWORD')
    expect(client, secret, b'CONFIRM PASSWORD')
    expect(client, secret, b'FILEBOARD UPDATED')


def run(binary, ctl, sqlite):
    with tempfile.TemporaryDirectory(prefix='vault-management-') as directory:
        root = Path(directory)
        vault, admins = root / 'vault', root / 'admins'
        source = root / 'source'
        payload = b'preserved payload\n'
        source.write_bytes(payload)
        subprocess.run([ctl, 'init', str(vault)], check=True, capture_output=True)
        for n in (1, 2):
            subprocess.run([ctl, 'import', str(vault), str(source), f'file{n}.txt', 'original'],
                           check=True, capture_output=True)

        def start():
            return Server(binary, root / 'notes', members=root / 'members', vault=vault,
                          language='en', admins=admins if admins.exists() else None,
                          options=['--database', str(root / 'store.sqlite')] if sqlite else [])

        with closing(start()) as server:
            for name in ('admin', 'sigop', 'owner', 'reader'):
                with closing(Client(server.port)) as c:
                    auth(c, name, True)
        admins.write_text('1\n', encoding='ascii')
        with closing(start()) as server, ExitStack() as stack:
            a, g, o, b, guest = [stack.enter_context(closing(Client(server.port))) for _ in range(5)]
            for client, name in ((a, 'admin'), (g, 'sigop'), (o, 'owner'), (b, 'reader')):
                auth(client, name)
            expect(guest, 'FILEDESC 1 forbidden', b'ERR login required')
            for path in ('-', 'parent', 'parent/child', 'parent/target', 'other'):
                expect(a, f'FILEBOARDSET {path}|member|member', b'FILEBOARD UPDATED')
            expect(a, 'FILECATEGORY 1 parent/child', b'FILE CATEGORY 1')
            expect(a, 'FILECATEGORY 2 other', b'FILE CATEGORY 2')
            expect(a, 'FILEOWNER 1 3', b'FILE OWNER CHANGED 1')
            expect(a, 'FILEBOARDSIGOP parent|2', b'FILEBOARD UPDATED')
            expect(o, 'FILEDESC 1 owner description', b'FILE EDITED 1')
            expect(o, 'FILEASCII 1 ON', b'FILE ATTRIBUTE CHANGED 1')
            for command in ('FILERENAME 1 forbidden', 'FILEOWNER 1 4', 'FILEHIDE 1 CONFIRM',
                            'FILERESTORE 1 CONFIRM', 'FILECOPY 1 parent/target'):
                expect(o, command, b'ERR vault denied')
            expect(b, 'FILEDESC 1 forbidden', b'ERR vault denied')
            expect(b, 'FILEDELETE 1 CONFIRM', b'ERR vault denied')
            expect(g, 'FILERENAME 1 ../escape', b'ERR vault syntax')
            expect(g, 'FILERENAME 1 renamed.txt', b'FILE RENAMED 1')
            expect(g, 'FILECOPY 1 other', b'ERR vault denied')
            expect(g, 'FILECOPY 2 parent/target', b'ERR vault denied')
            expect(g, 'FILECOPY 1 parent/target', b'FILE COPIED 1 3')
            detail = rows(o, 'FILE 3', b'END FILE')
            assert all(row in detail for row in (b'NAME renamed.txt', b'DESCRIPTION owner description',
                       b'PUBLISHER legacy-member:3', b'CATEGORY parent/target', b'ASCII ON', b'PROTOCOL COPY')), detail
            assert (vault / '00000003.bin').read_bytes() == payload
            expect(g, 'FILECATEGORY 3 other', b'ERR vault denied')
            expect(g, 'FILECATEGORY 3 parent/child', b'FILE CATEGORY 3')
            expect(g, 'FILEHIDE 1 CONFIRM', b'FILE HIDDEN 1')
            expect(b, 'FILE 1', b'ERR no file')
            expect(g, 'FILERESTORE 1 CONFIRM', b'FILE RESTORED 1')
            assert b'NAME renamed.txt' in rows(b, 'FILE 1', b'END FILE')
            expect(o, 'FILEDELETE 3 CONFIRM', b'FILE DELETED 3')
            assert not (vault / '00000003.bin').exists()
            expect(a, 'FILERESTORE 3 CONFIRM', b'ERR no file')
            expect(g, 'FILECOPY 1 parent/target', b'FILE COPIED 1 4')
            assert (vault / '00000004.bin').read_bytes() == payload
            expect(g, 'FILEBOARDSET other|none|none', b'ERR vault denied')
            expect(g, 'FILEBOARDNAME parent/child|Child title', b'FILEBOARD UPDATED')
            expect(o, 'FILEINFOEDIT parent/child', b'ERR vault denied')
            assert b'/SAVE' in cmd(g, 'FILEINFOEDIT parent/child')
            cmd(g, '/EDITOR BETA')
            g.send(b'First guide line\nSecond guide line\n')
            expect(g, '/save', b'FILEBOARD UPDATED')
            info = b'\n'.join(rows(o, 'FILEBOARDINFO parent/child', b'END FILEBOARD'))
            assert b'Child title' in info and b'First guide line' in info and b'Second guide line' in info
            with closing(LineConnector('127.0.0.1', server.port, 'reader', PASSWORD)) as api:
                for _ in range(2):
                    receive(api, 1)
                    api.complete_download(1)
            expect(g, 'FILEBOARDOPTIONS parent/child|BOARD 2 1', b'FILEBOARD UPDATED')
            assert counts(a) == 2
            assert sum(row.startswith(b'DOWNLOADROW ') for row in rows(a, 'FILEHISTORY 1')) == 1
            expect(g, 'FILEBOARDOPTIONS parent/child|MENU 2 1', b'ERR vault syntax')
            password(a, 'parent', 'Vault-password-1')
            expect(o, 'FILE 1', b'ERR no file')
            expect(o, 'FILEUNLOCK parent', b'PASSWORD')
            expect(o, 'wrong-password', b'ERR vault denied')
            expect(o, 'FILEUNLOCK parent', b'PASSWORD')
            assert cmd(o, 'Vault-password-1').startswith(b'FILEBOARD UNLOCKED ')
            assert b'NAME renamed.txt' in rows(o, 'FILE 1', b'END FILE')
            password(a, 'parent', 'Vault-password-2')
            expect(o, 'FILE 1', b'ERR no file')
            expect(a, 'FILEBOARDPASS parent NONE CONFIRM', b'FILEBOARD UPDATED')
            # Parent delegation, then live revocation: no cached management grant.
            expect(g, 'FILEBOARDSIGOP parent/child|4', b'FILEBOARD UPDATED')
            expect(a, 'FILEBOARDSIGOP parent|NONE', b'FILEBOARD UPDATED')
            expect(g, 'FILERENAME 1 forbidden', b'ERR vault denied')
            expect(g, 'FILEINFOEDIT parent/child', b'ERR vault denied')
            expect(b, 'FILERENAME 1 final.txt', b'FILE RENAMED 1')
            expect(o, 'FILEDESC 1 final description', b'FILE EDITED 1')
            expect(b, 'FILEAUDIT', b'ERR vault denied')
            audit = pages(a, 'FILEAUDIT', 'FILEAUDIT {}')
            assert any(b' N\t' in row for row in audit if row.startswith(b'VEVENT ')), audit
            assert hashlib.sha256((vault / '00000001.bin').read_bytes()).digest() == hashlib.sha256(payload).digest()
        with closing(start()) as server, closing(Client(server.port)) as a, closing(Client(server.port)) as o:
            auth(a, 'admin')
            auth(o, 'owner')
            detail = rows(o, 'FILE 1', b'END FILE')
            assert b'NAME final.txt' in detail and b'DESCRIPTION final description' in detail
            expect(o, 'FILE 3', b'ERR no file')
            assert counts(a) == 2
            assert sum(row.startswith(b'DOWNLOADROW ') for row in rows(a, 'FILEHISTORY 1')) == 1
            assert b'PROTOCOL COPY' in rows(o, 'FILE 4', b'END FILE')
    print('PASS: vault management, owner/inherited SIGOP/revocation, copy/reclaim, guide/password, retention/restart; ' +
          ('SQLite' if sqlite else 'journals'))


if __name__ == '__main__':
    binary, ctl = (str(Path(arg).resolve()) for arg in sys.argv[1:3])
    for sqlite in (False, True):
        run(binary, ctl, sqlite)
