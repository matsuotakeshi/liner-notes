"""Real profile service boundaries: visibility, paging, admin edits and restart."""
import pathlib
import sys
import tempfile

from test_board import Server
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd, rows


def profile(client, command):
    return rows(client, command, b'END PROFILE')


def identifiers(lines):
    return [int(line.split()[0]) for line in lines if line[:1].isdigit()]


def run(binary):
    binary = str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='profile-service-') as tmp:
        p = pathlib.Path(tmp)
        board, members, admins = p/'board', p/'members', p/'admins'
        server = Server(binary, board, members=members)
        try:
            for name in ('admin', 'author', 'viewer', *(f'user{i}' for i in range(9))):
                client = Client(server.port)
                try:
                    auth(client, name, True)
                finally:
                    client.close()
        finally:
            server.close()
        admins.write_text('1\n')
        options = dict(members=members, admins=admins)
        server = Server(binary, board, **options)
        a, c, v, guest = [Client(server.port) for _ in range(4)]
        try:
            for client, name in ((a, 'admin'), (c, 'author'), (v, 'viewer')):
                auth(client, name)
            assert cmd(guest, 'PROFILEID 2') == b'ERR login required'
            assert cmd(guest, 'PROFILES secret') == b'ERR login required'
            for user in range(1, 13):
                assert cmd(a, f'MEMBERPROFILE {user} LOCATION shared needle') == b'PROFILE SAVED'
            assert cmd(c, 'PROFILEVIS LOCATION PRIVATE') == b'PROFILE SAVED'
            assert cmd(a, 'MEMBERPROFILE 2 REALNAME secret-person') == b'PROFILE SAVED'
            assert cmd(a, 'MEMBERPROFILE 2 BIRTHDAY 2000-01-02') == b'PROFILE SAVED'
            assert cmd(c, 'PROFILEVIS BIRTHDAY MONTHDAY') == b'PROFILE SAVED'
            visible = profile(v, 'PROFILE author')
            assert visible[0] == b'PROFILE 2 author'
            assert b'BIRTHDAY 01-02' in visible
            assert not any(b'secret-person' in line or b'2000-01-02' in line or b'shared needle' in line for line in visible)
            assert b'REALNAME secret-person' in profile(c, 'PROFILE')
            assert b'REALNAME secret-person' in profile(a, 'PROFILEID 2')
            assert cmd(v, 'PROFILE Author') == b'ERR profile unavailable'
            assert cmd(v, 'PROFILE missing') == b'ERR profile unavailable'
            assert not identifiers(rows(v, 'PROFILES secret-person'))
            assert not identifiers(rows(v, 'PROFILEFIND REALNAME secret-person'))
            assert not identifiers(rows(v, 'PROFILEFIND BIRTHDAY 2000'))
            assert identifiers(rows(v, 'PROFILEFIND BIRTHDAY 01-02')) == [2]
            first = rows(v, 'PROFILEFIND LOCATION needle')
            assert identifiers(first) == [1, 3, 4, 5, 6, 7, 8, 9], first
            cursor = int(first[-1].split()[-1])
            assert cursor == 10
            # Retain the field filter; another field matching is not sufficient.
            assert cmd(a, 'MEMBERPROFILE 10 LOCATION different') == b'PROFILE SAVED'
            assert cmd(a, 'MEMBERPROFILE 10 INTERESTS needle') == b'PROFILE SAVED'
            second = rows(v, f'PROFILESFROM {cursor} needle')
            assert identifiers(second) == [11, 12] and second[-1] == b'END NEXT 0', second
            assert 10 in identifiers(rows(v, 'PROFILES needle')) or 10 in identifiers(rows(v, 'PROFILESFROM 10 needle'))
            assert cmd(a, 'GROUP 4 100').startswith(b'RIGHTS 4 ')
            assert cmd(a, 'GROUP 12 100').startswith(b'RIGHTS 12 ')
            assert identifiers(rows(v, 'PROFILEFIND GROUP 100')) == [4, 12]
            journal = pathlib.Path(str(members)+'.profile')
            size = journal.stat().st_size
            assert cmd(v, 'MEMBERPROFILE 2 REALNAME forged') == b'ERR profile permission or value'
            assert cmd(a, 'MEMBERPROFILE 9999 REALNAME missing') == b'ERR profile permission or value'
            assert cmd(a, 'MEMBERPROFILE 2 BIRTHDAY 1900-02-29') == b'ERR profile permission or value'
            assert journal.stat().st_size == size
            assert b'REALNAME secret-person' in profile(c, 'PROFILE')
            lines = [f'Line {i} mixed case' for i in range(24)]
            assert cmd(c, 'BIOEDIT') == b'DOCUMENT EDIT'
            for line in lines:
                c.send((line+'\n').encode())
            assert cmd(c, '/save') == b'PROFILE SAVED'
            expected = ('\r\n'.join(lines)+'\r\n').encode()
            data = profile(v, 'PROFILEID 2')
            marker = data.index(f'PROFILEBODY {len(expected)}'.encode())
            assert data[marker+1:marker+1+len(lines)] == [line.encode() for line in lines]
            assert cmd(a, 'SUSPEND 2') == b'SUSPENDED 2'
            assert cmd(v, 'PROFILE author') == b'ERR profile unavailable'
            assert not identifiers(rows(v, 'PROFILES author'))
            assert cmd(a, 'RESUME 2') == b'RESUMED 2'
        finally:
            for client in (a, c, v, guest):
                client.close()
            server.close()
        server = Server(binary, board, **options)
        a, c, v = [Client(server.port) for _ in range(3)]
        try:
            for client, name in ((a, 'admin'), (c, 'author'), (v, 'viewer')):
                auth(client, name)
            assert b'REALNAME secret-person' in profile(c, 'PROFILE')
            assert b'BIRTHDAY 2000-01-02' in profile(a, 'PROFILE author')
            data = profile(v, 'PROFILEID 2')
            assert b'BIRTHDAY 01-02' in data and b'Line 23 mixed case' in data
            assert not any(b'secret-person' in line for line in data)
            assert identifiers(rows(v, 'PROFILEFIND GROUP 100')) == [4, 12]
            assert cmd(a, 'MEMBERPROFILE 2 REALNAME after-restart') == b'PROFILE SAVED'
            assert b'REALNAME after-restart' in profile(c, 'PROFILE')
            assert not identifiers(rows(v, 'PROFILES after-restart'))
        finally:
            for client in (a, c, v):
                client.close()
            server.close()
    print('PASS profile service: privacy, retained paging, admin edits, long BIO and restart')


if __name__ == '__main__':
    run(sys.argv[1])
