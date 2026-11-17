"""Real-socket catalog policy/history regression for journals and SQLite."""
from contextlib import closing, ExitStack
from pathlib import Path
import os
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd, rows


def expect(c,command,wanted):
    got=cmd(c,command)
    assert got==wanted,(command,got,wanted)


def history(c):
    result=[];cursor=0
    for _ in range(12):
        page=rows(c,'BOARDLOG'+(f' {cursor}' if cursor else ''))
        assert page[0]==b'BOARDLOG',page
        result.extend(row for row in page if row.startswith(b'BOARDCHANGE '))
        cursor=int(page[-1].split()[-1])
        if not cursor:return result
    raise AssertionError('history cursor did not finish')


def run(binary,sqlite):
    with tempfile.TemporaryDirectory(prefix='catalog-core-') as directory:
        root=Path(directory);store=root/'notes';admins=root/'admins'
        def start():
            return Server(binary,store,members=root/'members',language='en',
                          admins=admins if admins.exists() else None,
                          options=['--database',str(root/'database.sqlite')] if sqlite else [])
        with closing(start()) as server:
            for name in ('admin','sigop','member'):
                with closing(Client(server.port)) as c:auth(c,name,True)
        admins.write_text('1\n',encoding='ascii')
        with closing(start()) as server, ExitStack() as stack:
            def login(name):
                c=stack.enter_context(closing(Client(server.port)));auth(c,name);return c
            a=login('admin')
            for spec in ('2|1|all|member|Managed|parent','3|2|all|member|Child|child','4|1|all|member|Outside|outside-secret'):
                expect(a,'BOARDSET '+spec,('BOARD UPDATED '+spec.split('|')[0]).encode())
            expect(a,'SIGOP 2 2',b'SIGOP SET 2 2')
            s=login('sigop');m=login('member')
            expect(s,'BOARDINFO 3',b'BOARDSETTING 3|2|all|member|Child|child')
            expect(s,'BOARDINFO 4',b'ERR admin required')
            expect(m,'BOARDINFO 2',b'ERR admin required')
            assert rows(m,'SIGOPS',b'END SIGOPS')==[b'SIGOPS',b'END SIGOPS']
            assert rows(s,'SIGOPS',b'END SIGOPS')==[b'SIGOPS',b'SIGOP 2 2',b'SIGOP 3 0',b'END SIGOPS']
            expect(s,'BOARDSET 5|3|all|member|Created|delegated',b'BOARD UPDATED 5')
            expect(s,'BOARDSET 5|2|all|member|Moved|within scope',b'BOARD UPDATED 5')
            expect(s,'BOARDSET 5|4|all|member|Escape|denied',b'ERR catalog syntax')
            expect(s,'BOARDSET 4|2|all|member|Take over|denied',b'ERR catalog syntax')
            expect(s,'BOARDSET 2|5|all|member|Cycle|denied',b'ERR catalog syntax')
            expect(s,'BOARDINFO 5',b'BOARDSETTING 5|2|all|member|Moved|within scope')
            response=cmd(s,'BOARD 2');assert response.startswith(b'BOARD 2'),response
            for i in range(18):
                expect(a,f'BOARDSET 3|2|all|member|Child {i}|kept',b'BOARD UPDATED 3')
                if i%5==0:expect(a,f'BOARDSET 4|1|all|member|Outside {i}|outside-secret',b'BOARD UPDATED 4')
            saved=history(s)
            assert len(saved)==22,saved  # boards 2/3/5, move 5 and 18 edits
            assert b'outside-secret' not in b'\n'.join(saved),saved
            sequence=[int(row.split()[1]) for row in saved]
            assert sequence==sorted(set(sequence),reverse=True),sequence
            expect(s,'BOARDLOG 4294967295',b'ERR command')
            # Changed scope is rechecked when the next page is requested.
            first=rows(s,'BOARDLOG');assert first[-1]!=b'END NEXT 0',first
            expect(a,'SIGOP 2 NONE',b'SIGOP SET 2 0')
            s.close();s=login('sigop')
            response=cmd(s,'BOARD 2');assert response.startswith(b'BOARD 2'),response
            expect(s,'BOARDLOG '+first[-1].split()[-1].decode(),b'ERR admin required')
            expect(s,'BOARDINFO 3',b'ERR admin required')
            expect(a,'SIGOP 2 2',b'SIGOP SET 2 2')
        with closing(start()) as server, ExitStack() as stack:
            a=stack.enter_context(closing(Client(server.port)));auth(a,'admin')
            s=stack.enter_context(closing(Client(server.port)));auth(s,'sigop')
            response=cmd(s,'BOARD 2');assert response.startswith(b'BOARD 2'),response
            expect(s,'BOARDINFO 3',b'BOARDSETTING 3|2|all|member|Child 17|kept')
            expect(s,'BOARDINFO 5',b'BOARDSETTING 5|2|all|member|Moved|within scope')
            assert history(s)==saved
            role_rows=rows(s,'SIGOPS',b'END SIGOPS');assert b'SIGOP 5 0' in role_rows,role_rows
            if not sqlite:
                # Fail after two valid rows: no partial history header or record
                # may escape. This is an isolated temporary journal only.
                journal=Path(str(store)+'.catalog');original=journal.read_bytes()
                assert original[:8]==b'LNCAT001' and (len(original)-8)%4096==0
                offset=len(original)-3*4096+28
                with journal.open('r+b') as f:
                    f.seek(offset);f.write(bytes([original[offset]^1]));f.flush();os.fsync(f.fileno())
                expect(a,'BOARDLOG',b'ERR catalog storage')
                expect(a,'BOARDINFO 3',b'ERR catalog storage')
    print('PASS: catalog scopes, pagination, roles and restart; SQLite='+str(sqlite))


if __name__=='__main__':
    binary=str(Path(sys.argv[1]).resolve())
    for sqlite in (False,True):run(binary,sqlite)
