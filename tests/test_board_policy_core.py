"""Socket regression for managed policy/guide/password/retention services."""
from contextlib import closing, ExitStack
from pathlib import Path
import sys
import tempfile
import time
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd,rows
from test_stage60 import password,enter
SECRET='Policy-Core-Test-Only-8872'


def expect(c,command,wanted):
    # Password prompts share the one-second post-login credential throttle.
    # Wait before exercising ACLs; ERR try later must not count as ACL denial.
    if command.startswith('BOARDPASS ') and len(command.split())==2:
        time.sleep(1.05)
    got=cmd(c,command);assert got==wanted,(command,got,wanted)


def run(binary,sqlite):
    with tempfile.TemporaryDirectory(prefix='policy-core-') as directory:
        root=Path(directory);store=root/'notes';admins=root/'admins'
        def start():
            return Server(binary,store,members=root/'members',language='en',
                          admins=admins if admins.exists() else None,
                          options=['--database',str(root/'database.sqlite')] if sqlite else [])
        with closing(start()) as server:
            for name in ('admin','manager','member'):
                with closing(Client(server.port)) as c:auth(c,name,True)
        admins.write_text('1\n',encoding='ascii')
        with closing(start()) as server, ExitStack() as stack:
            def login(name):
                c=stack.enter_context(closing(Client(server.port)));auth(c,name);return c
            a=login('admin')
            for value in ('2|1|all|member|Parent|parent','3|2|all|member|Child|child','4|1|all|member|Outside|outside'):
                expect(a,'BOARDSET '+value,('BOARD UPDATED '+value.split('|')[0]).encode())
            expect(a,'SIGOP 2 2',b'SIGOP SET 2 2')
            manager=login('manager');member=login('member')
            expect(manager,'BOARDOPTIONS 3 BOARD 7 0 0 3',b'BOARD OPTIONS SAVED')
            expect(manager,'BOARDOPTIONS 3',b'BOARDOPTIONS 3 BOARD ORDER 7 ARTICLES 0 BYTES 0 LOG 3 PASSWORD OFF')
            for command in ('BOARDOPTIONS 4','BOARDPASS 4','INFOEDIT 4','BOARDACCESS 4'):
                expect(manager,command,b'ERR admin required')
            expect(member,'BOARDOPTIONS 3',b'ERR admin required')
            expect(manager,'INFOEDIT 3',b'BOARD INFO EDIT')
            manager.send('Managed guide 日本語\r\nSecond line\r\n'.encode())
            expect(manager,'/save',b'BOARD INFO SAVED')
            guide=rows(member,'INFO 3',b'END INFO');assert 'Managed guide 日本語'.encode() in guide,guide
            password(manager,2,SECRET)
            expect(manager,'BOARDOPTIONS 2',b'BOARDOPTIONS 2 BOARD ORDER 2 ARTICLES 0 BYTES 0 LOG 1000 PASSWORD ON')
            enter(member,3,SECRET)
            article=post(member,'retained article','original body')
            expect(manager,'BOARDOPTIONS 3 MENU 7 0 0 3',b'ERR board not empty')
            for _ in range(5):assert read(member,article)[2]=='original body\r\n'
            log=rows(manager,'BOARDACCESS 3');events=[r for r in log if r.startswith(b'ACCESSROW ')]
            assert len(events)==3 and all(b' R ' in row for row in events),log
            expect(manager,'BOARDOPTIONS 3 BOARD 7 0 0 2',b'BOARD OPTIONS SAVED')
            assert len([r for r in rows(manager,'BOARDACCESS 3') if r.startswith(b'ACCESSROW ')])==2
            expect(member,'BOARDACCESS 3',b'ERR admin required')
            # Force pruning is durable, not merely a display filter.
            if not sqlite:assert Path(str(store)+'.boardaccess').stat().st_size==8+2*4096
        with closing(start()) as server, ExitStack() as stack:
            a=stack.enter_context(closing(Client(server.port)));auth(a,'admin')
            manager=stack.enter_context(closing(Client(server.port)));auth(manager,'manager')
            member=stack.enter_context(closing(Client(server.port)));auth(member,'member')
            expect(manager,'BOARDOPTIONS 3',b'BOARDOPTIONS 3 BOARD ORDER 7 ARTICLES 0 BYTES 0 LOG 2 PASSWORD OFF')
            assert len([r for r in rows(manager,'BOARDACCESS 3') if r.startswith(b'ACCESSROW ')])==2
            enter(member,3,SECRET)
            guide=rows(member,'INFO 3',b'END INFO');assert 'Managed guide 日本語'.encode() in guide,guide
            assert read(member,article)[2]=='original body\r\n'
            expect(manager,'BOARDPASS 2 NONE CONFIRM',b'BOARD PASSWORD SAVED')
            expect(manager,'BOARDOPTIONS 2',b'BOARDOPTIONS 2 BOARD ORDER 2 ARTICLES 0 BYTES 0 LOG 1000 PASSWORD OFF')
            expect(manager,'BOARDOPTIONS 3 BOARD 7 0 0 0',b'BOARD OPTIONS SAVED')
            assert rows(manager,'BOARDACCESS 3')==[b'BOARDACCESS 3',b'END NEXT 0']
            expect(a,'SIGOP 2 NONE',b'SIGOP SET 2 0')
            manager.close();manager=stack.enter_context(closing(Client(server.port)));auth(manager,'manager')
            for command in ('BOARDOPTIONS 3','BOARDPASS 2','INFOEDIT 3','BOARDACCESS 3'):
                expect(manager,command,b'ERR admin required')
        if not sqlite:assert SECRET.encode() not in Path(str(store)+'.boardpolicy').read_bytes()
    print('PASS: policy management, inherited password, guide, retention and restart; SQLite='+str(sqlite))


if __name__=='__main__':
    binary=str(Path(sys.argv[1]).resolve())
    for sqlite in (False,True):run(binary,sqlite)
