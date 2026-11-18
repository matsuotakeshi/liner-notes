"""Real sessions: public replay across restart and private-message exclusion."""
from contextlib import closing
from pathlib import Path
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth
from test_chat import until
from test_talk import command


def enter(client):
    client.send(b'CHAT\n');client.expect(b'CHAT OPEN')
    own=b'CHAT JOIN '+client.greeting.split()[1]+b' '
    rows=[]
    for _ in range(80):
        line=client.line();rows.append(line)
        if line.startswith(own):return rows
    raise AssertionError(rows)


def scenario(binary,sqlite,persist):
    with tempfile.TemporaryDirectory(prefix='chat-core-') as directory:
        root=Path(directory)
        options=['--chat-log',str(root/'public.log') if persist else '-']
        if sqlite:options+=['--database',str(root/'data.sqlite')]
        def start():
            return Server(binary,root/'notes',members=root/'members',language='en',options=options)
        with closing(start()) as server:
            with closing(Client(server.port)) as a, closing(Client(server.port)) as b:
                auth(a,'alice',True);auth(b,'bob',True)
                enter(a);enter(b)
                a.send('public-persist 日本語\n'.encode())
                assert until(a,b'CHAT SAY ').endswith('public-persist 日本語'.encode())
                assert until(b,b'CHAT SAY ').endswith('public-persist 日本語'.encode())
                aid=a.greeting.split()[1]
                command(b,'/TALK '+aid.decode(),b'CHAT TALK OPEN ')
                command(b,'private-never-replay',b'CHAT PRIVATE TO ')
                assert until(a,b'CHAT PRIVATE FROM ').endswith(b'private-never-replay')
        with closing(start()) as server, closing(Client(server.port)) as c:
            auth(c,'carol',True);rows=enter(c)
            assert not any(b'private-never-replay' in row or b'PRIVATE' in row for row in rows),rows
            public=[row for row in rows if row.startswith(b'CHAT SAY ')]
            assert len(public)==int(persist),rows
            if persist:assert public[0].endswith('public-persist 日本語'.encode()),public
            c.send(b'/LEAVE\n');c.expect(b'CHAT CLOSED')
            c.send(b'PING\n');c.expect(b'PONG')


def run(binary):
    binary=str(Path(binary).resolve())
    for sqlite,persist in [(False,True),(True,True),(False,False)]:scenario(binary,sqlite,persist)
    print('PASS chat core adapter: public restart replay, private exclusion, disabled journal, legacy and SQLite storage')


if __name__=='__main__':run(sys.argv[1])
