"""Destructive board services through sockets, journal/SQLite and restart."""
from contextlib import closing, ExitStack
from pathlib import Path
import sys
import tempfile
from test_board import Server, post, read
from test_runtime import Client
from test_members import auth, unread
from test_stage58 import cmd, rows


def expect(client, command, wanted):
    got=cmd(client,command)
    assert got==wanted,(command,got,wanted)


def board(client,number):
    got=cmd(client,f'BOARD {number}')
    assert got.startswith(f'BOARD {number} '.encode()),got


def run(binary,sqlite):
    with tempfile.TemporaryDirectory(prefix='topology-core-') as directory:
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
            for n,parent in ((2,1),(3,1),(4,2),(5,1),(6,1),(7,4)):
                expect(a,f'BOARDSET {n}|{parent}|all|member|Board{n}|test',f'BOARD UPDATED {n}'.encode())
            expect(a,'SIGOP 2 2',b'SIGOP SET 2 2')
            g=login('sigop');m=login('member')
            expect(a,'BOARDOPTIONS 6 MENU 6 0 0 1000',b'BOARD OPTIONS SAVED')
            board(a,2)
            first=post(a,'root','root body')
            child=post(a,'child','child body',first)
            leaf=post(a,'leaf','leaf body',child)
            hidden=post(a,'hidden','hidden body',first)
            removed=post(a,'removed','removed body',first)
            unrelated=post(a,'other root','other body')
            expect(a,f'HIDE {hidden}',f'HIDDEN SAVED {hidden}'.encode())
            expect(a,f'WITHDRAW {removed} CONFIRM',f'WITHDRAW SAVED {removed}'.encode())
            expect(a,f'SEEN {child}',f'SEEN {child}'.encode())
            for c in (g,m):
                board(c,2)
                for command in (f'MOVE {first} 3 CONFIRM',f'MOVEBRANCH {child} 3 CONFIRM',
                                'BOARDDELETE 4 CONFIRM','BOARDDELETEALL 2 CASCADE CONFIRM'):
                    expect(c,command,b'ERR admin required')
            expect(a,'BOARDDELETE 1 CONFIRM',b'ERR board not empty')
            expect(a,'BOARDDELETEALL 1 CASCADE CONFIRM',b'ERR command')
            expect(a,'BOARDDELETE 2 CONFIRM',b'ERR board not empty')
            expect(a,'BOARDDELETE 4 CONFIRM',b'ERR board not empty')
            expect(a,'BOARDDELETE 5 CONFIRM',b'BOARD DELETED')
            expect(a,'BOARDSET 5|1|all|member|Reuse|no',b'ERR catalog syntax')
            board(a,3);existing=post(a,'existing','existing body')
            board(a,2)
            for command in (f'MOVE {child} 3 CONFIRM',f'MOVE {first} 2 CONFIRM',
                            f'MOVE {first} 6 CONFIRM',f'MOVE {first} 5 CONFIRM'):
                expect(a,command,b'ERR move target')
            expect(a,'BOARDOPTIONS 3 BOARD 3 2 0 1000',b'BOARD OPTIONS SAVED')
            expect(a,f'MOVEBRANCH {child} 3 CONFIRM',b'ERR board full')
            expect(a,'BOARDOPTIONS 3 BOARD 3 3 1 1000',b'BOARD OPTIONS SAVED')
            expect(a,f'MOVEBRANCH {child} 3 CONFIRM',b'ERR board full')
            expect(a,'BOARDOPTIONS 3 BOARD 3 3 0 1000',b'BOARD OPTIONS SAVED')
            expect(a,f'MOVEBRANCH {child} 3 CONFIRM',f'THREAD MOVED {child} 3'.encode())
            expect(a,f'READ {child}',b'ERR no article')
            board(a,3)
            assert child not in unread(a)
            assert read(a,child)==(0,'child','child body\r\n')
            assert read(a,leaf)==(child,'leaf','leaf body\r\n')
            assert read(a,existing)[2]=='existing body\r\n'
            expect(a,'BOARDOPTIONS 3 BOARD 3 0 0 1000',b'BOARD OPTIONS SAVED')
            board(a,2)
            expect(a,f'MOVE {first} 3 CONFIRM',f'THREAD MOVED {first} 3'.encode())
            assert read(a,unrelated)[2]=='other body\r\n'
            expect(a,f'READ {first}',b'ERR no article')
            board(a,3)
            assert read(a,first)==(0,'root','root body\r\n')
            expect(a,f'READ {hidden}',f'HIDDEN {hidden} PARENT {first}'.encode())
            assert cmd(a,f'READ {removed}').startswith(f'WITHDRAWN {removed} '.encode())
            expect(a,f'RESTORE {hidden}',f'RESTORED {hidden}'.encode())
            assert read(a,hidden)==(first,'hidden','hidden body\r\n')
            # Access records remain attached to their original board.
            assert any(row.split()[2]==str(child).encode() for row in rows(a,'BOARDACCESS 2') if row.startswith(b'ACCESSROW '))
        with closing(start()) as server, closing(Client(server.port)) as a:
            auth(a,'admin');board(a,3)
            for ident,parent,title,body in ((first,0,'root','root body'),(child,0,'child','child body'),
                                           (leaf,child,'leaf','leaf body'),(hidden,first,'hidden','hidden body')):
                assert read(a,ident)==(parent,title,body+'\r\n')
            assert cmd(a,f'READ {removed}').startswith(f'WITHDRAWN {removed} '.encode())
            expect(a,'BOARDDELETEALL 2 CASCADE CONFIRM',b'BOARD TREE DELETED')
            for ident in (2,4,7):expect(a,f'BOARD {ident}',b'ERR board unavailable')
            board(a,3);assert read(a,leaf)[2]=='leaf body\r\n'
        with closing(start()) as server, closing(Client(server.port)) as a:
            auth(a,'admin')
            for ident in (2,4,5,7):expect(a,f'BOARD {ident}',b'ERR board unavailable')
            expect(a,'BOARDSET 2|1|all|member|Reuse|no',b'ERR catalog syntax')
            board(a,3)
            assert read(a,child)[0]==0 and read(a,leaf)[0]==child
            assert post(a,'next','after restart')>existing
    print('PASS: move/branch/delete/cascade authority, quotas, IDs and restart; SQLite='+str(sqlite))


if __name__=='__main__':
    binary=str(Path(sys.argv[1]).resolve())
    for sqlite in (False,True):run(binary,sqlite)
