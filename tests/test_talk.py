"""Private routing, shared-history exclusion, connection lifetime and Telnet UI."""
import pathlib
import sys
import tempfile
import time
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_chat import join,until
from test_telnet import Client as Telnet


def command(c,text,prefix):
    c.send(text.encode()+b'\n');return until(c,prefix)


def roster(c,forbidden=b'private-secret'):
    c.send(b'W\n');lines=[]
    while True:
        line=c.line();assert forbidden not in line and b'PRIVATE' not in line,line
        if line==b'CHAT END':return lines
        lines.append(line)


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='private-talk-') as d:
        p=pathlib.Path(d);data=p/'bbs';members=p/'members';admins=p/'admins'
        s=Server(binary,data,members=members)
        try:
            for name in ('alice','bob','carol'):
                c=Client(s.port)
                try:auth(c,name,True)
                finally:c.close()
        finally:s.close()
        admins.write_text('1\n');s=Server(binary,data,members=members,admins=admins)
        a,b,c,admin=[Client(s.port) for _ in range(4)]
        try:
            for client,name in ((a,'alice'),(b,'bob'),(c,'carol'),(admin,'alice')):auth(client,name)
            for client in (a,b,c):join(client)
            aid,bid=[x.greeting.split()[1].decode() for x in (a,b)]
            roster(a);roster(b);roster(c)
            command(a,'/TALK '+bid,b'CHAT TALK OPEN ')
            command(a,'private-secret こんにちは',b'CHAT PRIVATE TO '+bid.encode()+b' ')
            assert until(b,b'CHAT PRIVATE FROM '+aid.encode()+b' ').endswith('private-secret こんにちは'.encode())
            roster(c)
            command(b,'/REPLY',b'CHAT TALK OPEN '+aid.encode())
            command(b,'reply-private-secret',b'CHAT PRIVATE TO ')
            assert until(a,b'CHAT PRIVATE FROM ').endswith(b'reply-private-secret')
            roster(c)
            # Fresh participant and another connection of the recipient see no DM.
            extra=Client(s.port)
            try:
                auth(extra,'bob');extra.send(b'CHAT\n');extra.expect(b'CHAT OPEN')
                while True:
                    line=extra.line();assert b'private-secret' not in line and b'PRIVATE' not in line
                    if line.startswith(b'CHAT JOIN '+extra.greeting.split()[1]+b' '):break
                time.sleep(.55)
                command(a,'/SAY //',b'CHAT PRIVATE TO ');assert until(b,b'CHAT PRIVATE FROM ').endswith(b'//')
                roster(extra);roster(c)
            finally:extra.close()
            # Target leaves: sender remains in blocked private mode, never public.
            b.close();until(a,b'CHAT TALK GONE')
            command(a,'must-not-be-public',b'CHAT ERROR target')
            replacement=Client(s.port)
            try:
                auth(replacement,'bob');join(replacement)
                command(a,'still-must-not-be-public',b'CHAT ERROR target')
                roster(c,b'must-not-be-public');roster(replacement,b'must-not-be-public')
                command(a,'//',b'CHAT TALK CLOSED')
                # Explicit selection prompt; invalid selections stay private/blocked.
                a.send(b'/TALK\n');a.send(b'not-a-number\n');until(a,b'CHAT ERROR target')
                rid=replacement.greeting.split()[1].decode()
                command(a,rid,b'CHAT TALK OPEN '+rid.encode())
                command(a,'private-after-reselect',b'CHAT PRIVATE TO ');until(replacement,b'CHAT PRIVATE FROM ')
                command(admin,'SUSPEND 2',b'SUSPENDED 2');until(a,b'CHAT TALK GONE')
                command(a,'blocked-by-suspension',b'CHAT ERROR target');roster(c,b'blocked-by-suspension')
                command(admin,'RESUME 2',b'RESUMED 2')
            finally:replacement.close()
            command(a,'//',b'CHAT TALK CLOSED')
            for target in (aid,'999999999999999999999999999999','0','1234567'):
                command(a,'/TALK '+target,b'CHAT ERROR target')
                command(a,'no-public-fallback',b'CHAT ERROR target');roster(c,b'no-public-fallback')
                command(a,'//',b'CHAT TALK CLOSED')
            time.sleep(.55);command(a,'public-again',b'CHAT SAY ')
            assert until(c,b'CHAT SAY ').endswith(b'public-again')
            command(a,'//',b'CHAT CLOSED');command(c,'//',b'CHAT CLOSED')
        finally:a.close();b.close();c.close();admin.close();s.close()
        s=Server(binary,data,members=members,menu=True,telnet=True,language='en')
        a,b=Telnet(s.port),Telnet(s.port)
        try:
            ids=[]
            for client,name in ((a,'alice'),(b,'bob')):
                initial=client.until();ids.append(initial.split(b'RTBBS-PROBE ')[1].split()[0])
                client.cmd('LOGIN '+name,b'> ');client.cmd(PASSWORD);client.cmd('CHAT',b'Chat> ')
            a.until(b'Chat> ')
            prompt=b'Talk #'+ids[1]+b'> '
            assert b'Private talk' in a.cmd('/TALK '+ids[1].decode(),prompt)
            a.send('下書き'.encode());a.until('下書き'.encode())
            b.cmd('/TALK '+ids[0].decode(),b'Talk #'+ids[0]+b'> ')
            b.cmd('private incoming',b'Talk #'+ids[0]+b'> ')
            assert b'Private from' in a.until(prompt+'下書き'.encode())
            a.send(b'\r\n');assert '下書き'.encode() in a.until(prompt)
            assert '下書き'.encode() in b.until(b'Talk #'+ids[0]+b'> ')
            a.cmd('/LANG ja',('個別 #'+ids[1].decode()+'> ').encode())
            assert '共通チャット'.encode() in a.cmd('//','チャット> '.encode())
            a.cmd('//');b.until(b'Talk unavailable (// returns to room)> ')
            b.cmd('//',b'Chat> ');b.cmd('//')
        finally:a.close();b.close();s.close()
    print('PASS: private sender/recipient routing, reply, no history/third-party/same-account leaks, stale target and suspension blocking, explicit public return, bilingual Telnet private prompts and draft redraw')


if __name__=='__main__':run(sys.argv[1])
