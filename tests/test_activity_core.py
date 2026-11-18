"""Activity Core through the unchanged RAW command adapter, both storage modes."""
from contextlib import closing, ExitStack
from pathlib import Path
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth
from test_stage57 import through, gather
from test_stage58 import cmd, rows


def expect(c, command, expected):
    got=cmd(c,command)
    assert got==expected,(command,got,expected)


def run(binary, sqlite):
    with tempfile.TemporaryDirectory(prefix='activity-core-') as directory:
        root=Path(directory);admins=root/'admins';store=root/'notes'
        def start(extra=()):
            options=['--gather-count','2',*extra]
            if sqlite: options+=['--database',str(root/'database.sqlite')]
            return Server(binary,store,members=root/'members',language='en',
                          admins=admins if admins.exists() else None,options=options)
        with closing(start()) as server, ExitStack() as stack:
            a=stack.enter_context(closing(Client(server.port)))
            b=stack.enter_context(closing(Client(server.port)))
            auth(a,'alice',True);auth(b,'bob',True)
        admins.write_text('1\n',encoding='ascii')
        with closing(start()) as server, ExitStack() as stack:
            a=stack.enter_context(closing(Client(server.port)))
            b=stack.enter_context(closing(Client(server.port)))
            guest=stack.enter_context(closing(Client(server.port)))
            auth(a,'alice');auth(b,'bob')
            for command in ('GATHER READ','USERLOG','USERTABLE','USERSTATS'):
                expect(guest,command,b'ERR login required')
            for command in ('CONNECTIONS','ANNOUNCE denied','DISCONNECT 999 CONFIRM',
                            'SHUTDOWN 30 denied','SHUTDOWN CANCEL'):
                expect(b,command,b'ERR admin required')
                expect(guest,command,b'ERR admin required')
            for command,header,marker in (
                ('USERLOG',b'USERLOG',b'END NEXT'),
                ('USERTABLE',b'USERTABLE LOGIN LOGOUT SECONDS COUNT TRANSPORT',b'END NEXT'),
                ('USERSTATS',b'USERSTATS 2 LOGINS ',b'END USERSTATS')):
                own=rows(b,command,marker);assert own[0].startswith(header),own
                expect(b,command+' ALL',b'ERR admin required')
                expect(b,command+' 1',b'ERR admin required')
                expect(a,command+' 999',b'ERR no member')
                shared=rows(a,command+' ALL',marker)
                assert not any(x.startswith(b'ERR ') for x in shared),shared
            expect(b,'HANDLE gather-handle',b'PROFILE SAVED')
            gather(a,'discarded');gather(b,'retained');gather(a,'新'*70)
            a.send(b'GATHER READ\n');snapshot=through(a,b'GATHER END')
            assert b'discarded' not in snapshot and b'retained' in snapshot
            assert b'bob gather-handle' in snapshot and ('新'*70).encode() in snapshot
            assert snapshot.endswith(b'GATHER END NEXT 0'),snapshot
            # Cancel and invalid input must not append a Gather record.
            b.send(b'GATHER\n');through(b,b'GATHER END');assert b'70 characters' in b.line()
            b.send(('新'*71+'\n').encode());assert b'70 characters' in b.line()
            expect(b,'/CANCEL',b'CANCELLED')
            a.send(b'GATHER READ\n');assert through(a,b'GATHER END')==snapshot
            connections=rows(a,'CONNECTIONS',b'END CONNECTIONS')
            connection_id=int(guest.greeting.split()[1])
            assert any(x.startswith(f'CONNECTION {connection_id} guest '.encode()) for x in connections)
            expect(a,'ANNOUNCE activity-notice',b'ANNOUNCE SENT')
            expect(a,f'DISCONNECT {connection_id} CONFIRM',f'DISCONNECTED {connection_id}'.encode())
            assert not guest.sock.recv(1)
            expect(a,'DISCONNECT 999999 CONFIRM',b'ERR no connection')
            expect(a,'SHUTDOWN 30 activity-test',b'SHUTDOWN SCHEDULED 30')
            expect(a,'SHUTDOWN CANCEL',b'SHUTDOWN CANCELLED')
        # Reload uses the same retained bodies/order and account identity.
        with closing(start()) as server, closing(Client(server.port)) as a:
            auth(a,'alice');a.send(b'GATHER READ\n');assert through(a,b'GATHER END')==snapshot
        # Public and legacy compatibility modes admit all three report kinds,
        # but never give an anonymous connection access or confer admin effects.
        for policy in ('--userlog-public','--rtbbs-member-policy'):
            with closing(start([policy])) as server, ExitStack() as stack:
                b=stack.enter_context(closing(Client(server.port)))
                guest=stack.enter_context(closing(Client(server.port)));auth(b,'bob')
                for command in ('USERLOG','USERTABLE','USERSTATS'):
                    marker=b'END USERSTATS' if command=='USERSTATS' else b'END NEXT'
                    for target in ('1','ALL'):
                        result=rows(b,command+' '+target,marker)
                        assert not any(x.startswith(b'ERR ') for x in result),result
                    expect(guest,command+' ALL',b'ERR login required')
                expect(b,'SHUTDOWN CANCEL',b'ERR admin required')
        with closing(start(['--gather-count','0'])) as server, closing(Client(server.port)) as a:
            auth(a,'alice');expect(a,'GATHER READ',b'ERR feature disabled')
    print('PASS: activity commands, retained Gather/restart, USERLOG policies, runtime controls; '+('SQLite' if sqlite else 'journals'))


if __name__=='__main__':
    binary=str(Path(sys.argv[1]).resolve())
    for sqlite in (False,True): run(binary,sqlite)
