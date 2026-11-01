"""Shared-room real sockets, legacy controls, auth revocation and Telnet editing."""
import pathlib
import sys
import tempfile
import time
from test_board import Server,post
from test_runtime import Client
from test_members import auth, PASSWORD
from test_telnet import Client as Telnet
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from transfer_connectors import BinaryConnector,RemoteError


def until(c, prefix):
    for _ in range(80):
        line=c.line()
        if line.startswith(prefix):return line
    raise AssertionError(prefix)


def join(c):
    c.send(b'CHAT\n');c.expect(b'CHAT OPEN')
    ident=c.greeting.split()[1]
    return until(c,b'CHAT JOIN '+ident+b' ')


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='chat-') as d:
        p=pathlib.Path(d);data=p/'bbs';members=p/'members';admins=p/'admins'
        s=Server(binary,data,members=members)
        try:
            for name in ('alice','bob','carol'):
                c=Client(s.port)
                try:auth(c,name,True)
                finally:c.close()
        finally:s.close()
        admins.write_text('1\n')
        s=Server(binary,data,members=members,admins=admins)
        a,b,observer,admin=[Client(s.port) for _ in range(4)]
        try:
            observer.send(b'CHAT\n');observer.expect(b'ERR login required')
            auth(a,'alice');auth(b,'bob');auth(observer,'carol');auth(admin,'alice')
            a.send('HANDLE アリス\n'.encode());a.expect(b'PROFILE SAVED')
            assert 'アリス'.encode() in join(a)
            join(b);until(a,b'CHAT JOIN '+b.greeting.split()[1]+b' ')
            transfer=BinaryConnector('127.0.0.1',s.port,'carol',PASSWORD)
            a.send('こんにちは\n'.encode());assert until(a,b'CHAT SAY ').endswith('こんにちは'.encode())
            assert until(b,b'CHAT SAY ').endswith('こんにちは'.encode())
            try:
                try:transfer.info(1)
                except RemoteError as error:assert str(error)=='ERR vault disabled'
                else:raise AssertionError('unexpected file')
            finally:transfer.close()
            a.send(b'too fast\n');a.expect(b'CHAT ERROR rate')
            b.send(b'W\n');rows=[]
            while True:
                line=b.line()
                if line==b'CHAT END':break
                rows.append(line)
            assert len(rows)==4 and all(x.startswith(b'CHAT WHO ') for x in rows)
            assert {x.split()[2] for x in rows}=={x.greeting.split()[1] for x in (a,b,observer,admin)}
            assert post(observer,'BBS during chat','still available')==1
            # Ordinary BBS commands become text, never storage operations.
            before=data.stat().st_size;b.send(b'POST not-an-article\n')
            assert until(b,b'CHAT SAY ').endswith(b'POST not-an-article');until(a,b'CHAT SAY ')
            assert data.stat().st_size==before
            observer.send(b'PING\n');observer.expect(b'PONG')
            b.send(b'/NOPE\n');b.expect(b'CHAT ERROR command')
            b.send(b'\x1b[2J\n');b.expect(b'CHAT ERROR text')
            b.send(b'\xc0\xaf\n');b.expect(b'CHAT ERROR text')
            b.send(b'x'*513+b'\n');b.expect(b'CHAT ERROR text')
            time.sleep(.55);b.send(b'/SAY //\n');assert until(b,b'CHAT SAY ').endswith(b'//');until(a,b'CHAT SAY ')
            b.send(b'//\n');b.expect(b'CHAT CLOSED');until(a,b'CHAT LEAVE ')
            b.send(b'PING\n');b.expect(b'PONG')
            # New entrants see recent messages; normal BBS sessions never do.
            b.send(b'CHAT\n');b.expect(b'CHAT OPEN');assert until(b,b'CHAT SAY ').endswith('こんにちは'.encode())
            until(b,b'CHAT JOIN '+b.greeting.split()[1]+b' ');until(a,b'CHAT JOIN ')
            admin.send(b'SUSPEND 2\n');admin.expect(b'SUSPENDED 2');until(a,b'CHAT LEAVE ')
            until(b,b'ERR session revoked')
            b.close();admin.send(b'RESUME 2\n');admin.expect(b'RESUMED 2')
            a.send(b'//\n');a.expect(b'CHAT CLOSED')
            b=Client(s.port);auth(b,'bob');b.send(b'CHAT\n');b.expect(b'CHAT OPEN')
            until(b,b'CHAT JOIN '+b.greeting.split()[1]+b' ') # Public history now survives empty rooms.
            b.close();time.sleep(.15);join(a)
            # Slot reuse cannot retain the previous member's room membership.
            replacement=Client(s.port)
            try:replacement.send(b'PING\n');replacement.expect(b'PONG')
            finally:replacement.close()
            a.send(b'/QUIT\n');a.expect(b'CHAT CLOSED')
        finally:a.close();b.close();observer.close();admin.close();s.close()
        s=Server(binary,data,members=members,admins=admins,menu=True,telnet=True,language='en')
        a,b=Telnet(s.port),Telnet(s.port)
        try:
            for c,name in ((a,'alice'),(b,'bob')):
                assert b'CHAT' in c.until();c.cmd('LOGIN '+name,b'> ');c.cmd(PASSWORD);c.cmd('LANG en')
                assert b'CHAT enters' in c.cmd('HELP CHAT')
                c.cmd('CHAT',b'Chat> ')
            a.until(b'Chat> ')
            b.send('日本'.encode());b.until('日本'.encode())
            a.cmd('hello',b'Chat> ')
            received=b.until('Chat> 日本'.encode());assert b'hello' in received
            b.send(b'\x7f'+ '語\r\n'.encode())
            assert '日語'.encode() in b.until(b'Chat> ')
            assert '日語'.encode() in a.until(b'Chat> ')
            b.cmd('/LANG ja','チャット> '.encode())
            assert 'チャット室'.encode() in b.cmd('?','チャット> '.encode())
            b.cmd('//');a.until(b'Chat> ')
            a.cmd('//')
        finally:a.close();b.close();s.close()
    print('PASS: chat raw/Telnet broadcasts, WHO/help/leave, history privacy, no BBS side effects, guest/suspended rejection, disconnect cleanup and UTF-8 input redraw')


if __name__=='__main__':run(sys.argv[1])
