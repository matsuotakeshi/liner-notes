from preference_assertions import assert_preferences_only
"""Unified history remains durable, authorized and stable across both journals."""
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
from test_board import Server, post
from test_runtime import Client
from test_members import auth, PASSWORD
from test_moderation import expect
from test_audit import history
from test_ui import Menu


def checksum(row):
    h = 2166136261
    for b in row[:508]:
        h = ((h ^ b) * 16777619) & 0xffffffff
    struct.pack_into('<I', row, 508, h)


def run(binary):
    binary = str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='content-audit-') as d:
        p = pathlib.Path(d)
        data, users, admins, config = [p / n for n in ('board', 'users', 'admins', 'config')]
        extra = p / 'board.extras'
        s = Server(binary, data, members=users)
        a, b, c = [Client(s.port) for _ in range(3)]
        try:
            for client, name in [(a, 'alice'), (b, 'bob'), (c, 'carol')]:
                auth(client, name, True)
            assert post(b, 'original secret', 'secret body') == 1
        finally:
            a.close(); b.close(); c.close(); s.close()
        admins.write_text('1\n3\n')
        config.write_text('1|0|all|member|Public\n2|0|u3|u3|Private\n')
        args = dict(members=users, admins=admins, catalog=config)
        s = Server(binary, data, **args)
        a, b, c = [Client(s.port) for _ in range(3)]
        try:
            for client, name in [(a, 'alice'), (b, 'bob'), (c, 'carol')]:
                auth(client, name)
            for i in range(4):
                expect(b, f'RETITLE 1 title-{i}', 'RETITLED 1')
                expect(a, 'HIDE 1', 'HIDDEN SAVED 1')
                expect(a, 'RESTORE 1', 'RESTORED 1')
            expect(b, 'WITHDRAW 1', 'WITHDRAW CONFIRM 1')
            expect(c, 'WITHDRAW 1', 'WITHDRAW CONFIRM 1')
            expect(b, 'HANDLE Bob', 'PROFILE SAVED')
            expect(b, 'WITHDRAW 1 CONFIRM', 'WITHDRAW SAVED 1')
            expect(b, 'WITHDRAW 1 CONFIRM', 'WITHDRAW SAVED 1')
            expect(c, 'BOARD 2', 'BOARD 2 Private')
            assert post(c, 'private secret', 'private body') == 2
            expect(c, 'RETITLE 2 private revised', 'RETITLED 2')
            expect(c, 'WITHDRAW 2 CONFIRM', 'WITHDRAW SAVED 2')
            expect(b, 'AUDIT', 'ERR admin required')
            rows, cursor = history(a)
            assert [e[0] for e in rows] == list(range(13, 5, -1)) and cursor == 5
            assert rows[0][2:] == (2, 'bob', 'WITHDRAW', 1, 1)
            assert all(e[1].endswith('Z') for e in rows)
            assert [e[4] for e in rows[:4]] == ['WITHDRAW', 'RESTORE', 'HIDE', 'RETITLE']
            assert [e[0] for e in history(c)[0]][:2] == [15, 14]
            tail = history(a, f'AUDIT {cursor}')
            expect(a, 'SUSPEND 2', 'SUSPENDED 2')
            expect(a, 'RESUME 2', 'RESUMED 2')
            assert history(a, f'AUDIT {cursor}') == tail
        finally:
            a.close(); b.close(); c.close(); s.close()
        assert extra.stat().st_size == 8 + 7 * 512  # HANDLE now lives in the separate profile journal
        snapshot = {f: f.read_bytes() for f in (data, users, extra, p / 'board.moderation', p / 'users.profile')}
        s = Server(binary, data, **args, menu=True)
        a = Menu(s.port)
        try:
            assert 'AUDIT 操作履歴' not in a.until()
            assert '管理者権限が必要' in a.cmd('AUDIT REFRESH')
            a.cmd('LOGIN alice', '> '); a.cmd(PASSWORD)
            assert 'AUDIT 操作履歴' in a.cmd('MENU')
            view = a.cmd('AUDIT REFRESH')
            assert 'AUDIT REFRESH: 最新の履歴' in view
            assert '記事を取り消し' in view and '記事の題名を変更' in view and '2:bob' in view
            assert 'secret' not in view and 'title-' not in view and 'private' not in view
            assert 'Liner notes' in a.cmd('MENU')
            a.cmd('LANG en')
            assert 'AUDIT Operation history' in a.cmd('?')
            view = a.cmd('AUDIT REFRESH')
            assert 'AUDIT REFRESH: Latest' in view
            assert 'Withdraw article' in view and 'Change article title' in view
            assert '#1 ' in a.cmd('+')
        finally:
            a.close(); s.close()
        assert all(f.read_bytes() == original for f, original in snapshot.items() if f!=users)
        assert_preferences_only(snapshot[users],users.read_bytes())
        print('PASS: mixed history order, actors, UTC, ACLs, restart, stable cursors, localized UI and no title/body disclosure')

        # Older records have no timestamp or cross-journal ordering information.
        legacy = bytearray(snapshot[extra])
        for start in range(8, len(legacy), 512):
            row = bytearray(legacy[start:start+512])
            row[1] = 0; row[340:352] = bytes(12); checksum(row)
            legacy[start:start+512] = row
        extra.write_bytes(legacy)
        s = Server(binary, data, **args)
        a = Client(s.port)
        try:
            auth(a, 'alice')
            rows, _ = history(a, 'AUDIT 7')
            assert [e[0] for e in rows] == [5, 4, 3, 2, 1]
            assert all(e[1] == 'unknown' for e in rows)
            expect(a, 'RESTORE 1', 'ERR article withdrawn')
            assert post(a, 'new', 'new') == 3
            expect(a, 'RETITLE 3 newer', 'RETITLED 3')
            assert history(a)[0][0][0] == 18
        finally:
            a.close(); s.close()
        s = Server(binary, data, **args)
        a = Client(s.port)
        try:
            auth(a, 'alice')
            assert history(a)[0][0][0] == 18
            if os.name == 'posix':
                with extra.open('r+b') as f:
                    f.truncate(8)
                expect(a, 'AUDIT', 'ERR extras storage')
                expect(a, 'READ 3', 'ERR extras storage')
                expect(a, 'PING', 'PONG')
        finally:
            a.close(); s.close()
        print('PASS: legacy undated records, append after history reads and restart; POSIX read failure closes board access')

        # A valid checksum must not allow an impossible moderation anchor.
        bad = bytearray(snapshot[extra]); row = bytearray(bad[8:520])
        struct.pack_into('<I', row, 348, 999999); checksum(row); bad[8:520] = row
        extra.write_bytes(bad)
        result = subprocess.run([binary, '0', str(data), str(users), str(config)], capture_output=True, timeout=10)
        assert result.returncode and b'extras:' in result.stderr and extra.read_bytes() == bad
        print('PASS: invalid history metadata rejected without rewriting the journal')


if __name__ == '__main__':
    run(sys.argv[1])
