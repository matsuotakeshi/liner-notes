"""Vault query Core through real identity/ACL/RAW adapters and both stores."""
from contextlib import closing
from pathlib import Path
import subprocess
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd


def rows(client, text, end=b'END NEXT'):
    client.send((text + '\n').encode())
    result = []
    for _ in range(100):
        row = client.line()
        assert not row.startswith(b'ERR '), (text, row)
        result.append(row)
        if row.startswith(end):
            return result
    raise AssertionError(('missing terminator', text, result))


def expect(client, text, expected):
    actual = cmd(client, text)
    assert actual == expected, (text, actual, expected)


def ids(page):
    return [int(row.split()[1]) for row in page if row.startswith(b'FILEITEM ')]


def pages(client, first, continuation):
    result = []
    for _ in range(20):
        page = rows(client, first)
        result.extend(page)
        cursor = int(page[-1].split()[-1])
        if not cursor:
            return result
        first = continuation.format(cursor)
    raise AssertionError('query cursor did not terminate')


def run(binary, ctl, sqlite):
    with tempfile.TemporaryDirectory(prefix='vault-query-core-') as directory:
        root = Path(directory)
        vault, admins = root / 'vault', root / 'admins'
        source = root / 'source'
        source.write_bytes(b'query fixture')
        subprocess.run([ctl, 'init', str(vault)], check=True, capture_output=True)
        for number in range(1, 13):
            subprocess.run([ctl, 'import', str(vault), str(source),
                            f'file{number:02}.LZH', f'description {number:02}'],
                           check=True, capture_output=True)

        def start():
            return Server(binary, root / 'notes', members=root / 'members', vault=vault,
                          language='en', admins=admins if admins.exists() else None,
                          options=['--database', str(root / 'store.sqlite')] if sqlite else [])

        with closing(start()) as server, closing(Client(server.port)) as a, closing(Client(server.port)) as b:
            auth(a, 'alice', True)
            auth(b, 'bob', True)
        admins.write_text('1\n', encoding='ascii')
        with closing(start()) as server, closing(Client(server.port)) as a, closing(Client(server.port)) as b, closing(Client(server.port)) as guest:
            auth(a, 'alice')
            auth(b, 'bob')
            expect(guest, 'FILES', b'ERR login required')
            for path in ('-', 'pub', 'pub/leaf', 'private'):
                expect(a, f'FILEBOARDSET {path}|member|member', b'FILEBOARD UPDATED')
            for number, path in ((1, 'pub'), (2, 'pub/leaf'), (3, 'private')):
                expect(a, f'FILECATEGORY {number} {path}', f'FILE CATEGORY {number}'.encode())
            expect(a, 'FILEBOARDNAME pub/leaf|Leaf title', b'FILEBOARD UPDATED')
            expect(a, 'FILEBOARDSET private|none|none', b'FILEBOARD UPDATED')
            listed = pages(b, 'FILES', 'FILES {}')
            assert ids(listed) == [1, 2] + list(range(4, 13)), listed
            assert b'file03' not in b'\n'.join(listed)
            scoped = rows(b, 'FILESEARCH pub|*.lzh')
            assert ids(scoped) == [1, 2], scoped
            assert ids(rows(b, 'FILESIN pub')) == [1, 2]
            assert ids(rows(b, 'FILENEWS pub|2000-01-01')) == [1, 2]
            # An empty time-window page still has a continuation from its scan.
            empty = rows(b, 'FILENEWS @0:0')
            cursor = int(empty[-1].split()[-1])
            assert not ids(empty) and cursor, empty
            assert not ids(rows(b, f'FILENEWSFROM {cursor} @0:0'))
            expect(b, 'FILENEWS private|invalid-date', b'ERR vault denied')
            expect(b, 'FILESEARCH private|*', b'ERR vault denied')
            expect(b, 'FILE 3', b'ERR no file')
            expect(b, 'FILEAUDIT', b'ERR vault denied')
            detail = rows(b, 'FILE 1', b'END FILE')
            assert b'NAME file01.LZH' in detail and b'PROTOCOL UNKNOWN' in detail, detail
            children = rows(b, 'FILECHILDREN pub', b'END CHILDREN NEXT')
            assert b'CHILD 1 pub/leaf|Leaf title' in children, children
            board_rows = pages(b, 'FILEBOARDS', 'FILEBOARDS {}')
            assert b'private' not in b'\n'.join(board_rows)
            categories = pages(b, 'FILECATEGORIES', 'FILECATEGORIES {}')
            assert b'private' not in b'\n'.join(categories)
            # MATCH uses exactly the current (root) board, not its descendants.
            expect(b, 'BATCH MATCH *.lzh', b'BATCH 9 4,5,6,7,8,9,10,11,12')
            expect(a, 'FILEHIDE 1 CONFIRM', b'FILE HIDDEN 1')
            expect(b, 'FILE 1', b'ERR no file')
            assert ids(rows(b, 'FILESIN pub')) == [2]
            expect(a, 'FILEBOARDSET pub|none|none', b'FILEBOARD UPDATED')
            expect(b, 'FILESEARCH pub|*', b'ERR vault denied')
            expect(b, 'FILECHILDREN pub', b'ERR vault denied')
            expect(b, 'FILE 2', b'ERR no file')
            audit = pages(a, 'FILEAUDIT', 'FILEAUDIT {}')
            assert any(row.startswith(b'VEVENT ') for row in audit)
        with closing(start()) as server, closing(Client(server.port)) as b:
            auth(b, 'bob')
            assert ids(pages(b, 'FILES', 'FILES {}')) == list(range(4, 13))
            expect(b, 'FILE 1', b'ERR no file')
            expect(b, 'FILESEARCH pub|*', b'ERR vault denied')
    print('PASS: vault queries, identity/ACL/privacy, continuation, batch scope, restart; ' +
          ('SQLite' if sqlite else 'journals'))


if __name__ == '__main__':
    binary, ctl = (str(Path(arg).resolve()) for arg in sys.argv[1:3])
    for sqlite in (False, True):
        run(binary, ctl, sqlite)
