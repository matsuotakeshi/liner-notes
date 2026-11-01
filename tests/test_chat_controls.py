"""Reception controls, identity stability, explicit recovery and isolation."""
import pathlib,sys,tempfile,time
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_chat import join,until
from test_talk import command,roster
from test_telnet import Client as Telnet

def settings(c,text):
    c.send(text.encode()+b'\n');rows=[]
    while True:
        row=c.line()
        if row==b'CHAT END':return b'\n'.join(rows)
        rows.append(row)

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='chat-controls-') as d:
        p=pathlib.Path(d);s=Server(binary,p/'bbs',members=p/'members')
        clients=[]
        try:
            for name in ('alice','bob','carol'):
                c=Client(s.port);clients.append(c);auth(c,name,True);join(c)
            a,b,c=clients;aid,bid=[x.greeting.split()[1].decode() for x in (a,b)]
            for x in clients:roster(x)
            assert b'CHAT RECEIVE OFF' in settings(b,'/TALK OFF')
            command(a,'/TALK '+bid,b'CHAT TALK OPEN ')
            command(a,'blocked-secret',b'CHAT ERROR refused')
            roster(b,b'blocked-secret');roster(c,b'blocked-secret')
            assert b'CHAT RECEIVE ON' in settings(b,'/TALK ON')
            command(a,'accepted-secret',b'CHAT PRIVATE TO ');until(b,b'CHAT PRIVATE FROM ')
            assert b'CHAT MUTED alice' in settings(b,'/MUTE alice')
            time.sleep(.55);command(a,'muted-secret',b'CHAT ERROR refused');roster(b,b'muted-secret');roster(c,b'muted-secret')
            command(a,'//',b'CHAT TALK CLOSED');command(a,'muted-public',b'CHAT SAY ')
            assert until(c,b'CHAT SAY ').endswith(b'muted-public');roster(b,b'muted-public')
            # Same account on another connection cannot bypass the mute.
            other=Client(s.port);clients.append(other);auth(other,'alice');join(other)
            command(other,'/TALK '+bid,b'CHAT TALK OPEN ');command(other,'other-secret',b'CHAT ERROR refused')
            roster(b,b'other-secret')
            command(other,'/LEAVE',b'CHAT CLOSED');other.close();clients.remove(other)
            command(a,'/LEAVE',b'CHAT CLOSED')
            assert b'CHAT MUTED alice' in settings(b,'/MUTES')
            # Offline removal works, and unrelated users stay visible.
            assert b'CHAT MUTED alice' not in settings(b,'/UNMUTE alice')
            command(c,'visible-public',b'CHAT SAY ');assert until(b,b'CHAT SAY ').endswith(b'visible-public')
            command(b,'/MUTE missing',b'CHAT ERROR mute');command(b,'/MUTE bob',b'CHAT ERROR mute')
            join(a);roster(b);roster(c)
            settings(b,'/MUTE alice');settings(b,'/TALK OFF')
            command(b,'/LEAVE',b'CHAT CLOSED');join(b)
            status=settings(b,'/MUTES');assert b'CHAT RECEIVE OFF' in status and b'CHAT MUTED alice' in status
            settings(b,'/TALK ON');settings(b,'/UNMUTE alice')
            command(a,'/TALK '+bid,b'CHAT TALK OPEN ');time.sleep(.55)
            command(a,'reset-secret',b'CHAT PRIVATE TO ');until(b,b'CHAT PRIVATE FROM ')
            roster(c,b'reset-secret')
        finally:
            for c in clients:c.close()
            s.close()
        s=Server(binary,p/'bbs',members=p/'members',menu=True,telnet=True,language='en')
        a=Telnet(s.port)
        try:
            a.until();a.cmd('LOGIN alice',b'> ');a.cmd(PASSWORD)
            assert b'/MUTE' in a.cmd('HELP TALK')
            a.cmd('CHAT',b'Chat> ')
            assert b'Private reception: OFF' in a.cmd('/TALK OFF',b'Chat> ')
            a.cmd('/LANG ja','チャット> '.encode())
            assert '個別トーク受信: ON'.encode() in a.cmd('/TALK ON','チャット> '.encode())
            assert 'ログイン名'.encode() in a.cmd('/MUTE missing','チャット> '.encode())
            a.cmd('/LEAVE')
        finally:a.close();s.close()
    print('PASS: reception off/on, per-login private/public mute, same-account reconnection protection, unrelated delivery, offline unmute, visit persistence, bilingual controls and HELP TALK')

if __name__=='__main__':run(sys.argv[1])
