"""Board-group administration, session invalidation and recovery over sockets."""
from contextlib import closing, ExitStack
from pathlib import Path
import sqlite3
import sys
import tempfile
from test_board import Server, post, read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd


def expect(c,command,wanted):
    got=cmd(c,command);assert got==wanted,(command,got,wanted)


def enter(c,number):
    got=cmd(c,f'BOARD {number}');assert got.startswith(f'BOARD {number} '.encode()),got


def rights(c,user,group):
    got=cmd(c,f'RIGHTS {user}')
    assert got.startswith(f'RIGHTS {user} LEVEL '.encode()),got
    groups=got.split(b' GROUPS ')[1].split(b' STATUS ')[0].decode().split(',')
    assert groups==[str(group)],got


def run(binary,sqlite):
    with tempfile.TemporaryDirectory(prefix='catalog-group-') as d:
        root=Path(d);store=root/'notes';admins=root/'admins'
        database=root/'database.sqlite'
        features=database if sqlite else Path(str(store)+'.features.sqlite')
        def start():
            return Server(binary,store,members=root/'members',language='en',
                          admins=admins if admins.exists() else None,
                          options=['--database',str(database)] if sqlite else [])
        def delegation():
            with sqlite3.connect(features.as_uri()+'?mode=ro',uri=True) as db:
                return db.execute('SELECT board,gid FROM delegated ORDER BY board,gid').fetchall()
        with closing(start()) as server:
            for name in ('admin','sigop','alice','bob'):
                with closing(Client(server.port)) as c:auth(c,name,True)
        admins.write_text('1\n',encoding='ascii')
        with closing(start()) as server, ExitStack() as stack:
            def login(name):
                c=stack.enter_context(closing(Client(server.port)));auth(c,name);return c
            a=login('admin')
            for spec in ('2|1|member|member|Forum|parent','3|2|g10|g10|Child|group',
                         '4|1|g10|g10|Other|unmodified','5|3|member|member|Leaf|own ACL'):
                expect(a,'BOARDSET '+spec,('BOARD UPDATED '+spec.split('|')[0]).encode())
            expect(a,'SIGOP 2 2',b'SIGOP SET 2 2')
            for user in (3,4):
                assert cmd(a,f'GROUP {user} 10').startswith(f'RIGHTS {user} '.encode())
            g=login('sigop');m=login('alice');other=login('admin')
            for c in (g,m):
                expect(c,'SIGOPGROUP 3 10',b'ERR admin required')
                expect(c,'BOARDGROUP 3 10 11 CONFIRM',b'ERR admin required')
            for command in ('SIGOPGROUP 3 9','SIGOPGROUP 5 10','SIGOPGROUP 3 101',
                            'BOARDGROUP 3 9 11 CONFIRM','BOARDGROUP 3 10 10 CONFIRM',
                            'BOARDGROUP 3 10 101 CONFIRM','BOARDGROUP 99 10 11 CONFIRM'):
                expect(a,command,b'ERR command')
            for command in ('SIGOPGROUP 3 10','SIGOPGROUP 3 10','SIGOPGROUP 4 10'):
                expect(a,command,b'SIGOP GROUP SAVED')
            assert delegation()==[(3,10),(4,10)]
            expect(a,'SIGOPGROUP 3 0',b'SIGOP GROUP SAVED')
            assert delegation()==[(4,10)]
            expect(a,'SIGOPGROUP 3 10',b'SIGOP GROUP SAVED')
            enter(m,3);article=post(m,'before move','unchanged body')
            # Membership management still follows current SIGOP + ACL, not
            # merely the existence of an optional delegation record.
            enter(g,3);expect(g,'BOARDGROUP 3 10 11 CONFIRM',b'ERR admin required')
            expect(a,'BOARDGROUP 3 10 11 CONFIRM',b'BOARD GROUP MOVED')
            for c in (g,m,other):c.expect(b'ERR session revoked')
            assert cmd(a,'BOARDINFO 3').startswith(b'BOARDSETTING 3|2|g11|g11|')
            assert cmd(a,'BOARDINFO 4').startswith(b'BOARDSETTING 4|1|g10|g10|')
            for user in (3,4):rights(a,user,11)
            assert delegation()==[(3,11),(4,10)]
            # Old-source retry is invalid, not a second migration.
            expect(a,'BOARDGROUP 3 10 11 CONFIRM',b'ERR command')
            fresh=login('alice');enter(fresh,3)
            assert read(fresh,article)==(0,'before move','unchanged body\r\n')
            expect(fresh,'BOARD 4',b'ERR board unavailable')
        with closing(start()) as server, closing(Client(server.port)) as a, closing(Client(server.port)) as m:
            auth(a,'admin');auth(m,'alice');enter(m,3)
            assert read(m,article)[2]=='unchanged body\r\n'
            rights(a,3,11);rights(a,4,11)
            assert delegation()==[(3,11),(4,10)]
        # Simulate a committed migration intent left before journal application.
        # Only this stopped private fixture is edited, never running/BBS data.
        with sqlite3.connect(features) as db:
            db.execute('UPDATE group_moves SET source=11,destination=12,done=0 WHERE board=3')
            assert db.execute('SELECT changes()').fetchone()==(1,)
        for _ in range(2):
            with closing(start()) as server, closing(Client(server.port)) as a, closing(Client(server.port)) as m:
                auth(a,'admin');auth(m,'alice')
                assert cmd(a,'BOARDINFO 3').startswith(b'BOARDSETTING 3|2|g12|g12|')
                assert cmd(a,'BOARDINFO 4').startswith(b'BOARDSETTING 4|1|g10|g10|')
                rights(a,3,12);rights(a,4,12)
                enter(m,3);assert read(m,article)[2]=='unchanged body\r\n'
                expect(m,'BOARD 4',b'ERR board unavailable')
                assert delegation()==[(3,12),(4,10)]
            with sqlite3.connect(features.as_uri()+'?mode=ro',uri=True) as db:
                assert db.execute('SELECT count(*) FROM group_moves WHERE done=0').fetchone()==(0,)
    print('PASS: scoped delegation, global memberships, session revocation and repeated recovery; SQLite='+str(sqlite))


if __name__=='__main__':
    binary=str(Path(sys.argv[1]).resolve())
    for sqlite in (False,True):run(binary,sqlite)
