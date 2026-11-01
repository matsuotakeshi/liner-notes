"""Outside-room delivery without public chat participation or unsafe input."""
import pathlib,sys,tempfile,time
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_chat import join,until
from test_talk import command
from test_telnet import Client as Telnet

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='outside-talk-') as d:
        p=pathlib.Path(d);s=Server(binary,p/'bbs',members=p/'members');a,b,c=[Client(s.port) for _ in range(3)]
        try:
            for x,name in [(a,'alice'),(b,'bob'),(c,'carol')]:auth(x,name,True)
            bid=b.greeting.split()[1].decode();aid=a.greeting.split()[1].decode()
            a.send(b'ONLINE\n');rows=[]
            while True:
                line=a.line()
                if line==b'CHAT END':break
                rows.append(line)
            assert len(rows)==3
            command(a,'TALK '+bid,b'CHAT TALK OPEN ');command(a,'outside-secret',b'CHAT PRIVATE TO ');until(b,b'CHAT PRIVATE FROM ')
            command(b,'/REPLY',b'CHAT TALK OPEN '+aid.encode());command(b,'outside-reply',b'CHAT PRIVATE TO ');until(a,b'CHAT PRIVATE FROM ')
            command(b,'//',b'TALK CLOSED');command(b,'PING',b'PONG')
            join(c);command(c,'public-not-for-outsiders',b'CHAT SAY ')
            time.sleep(.55);command(b,'POST draft',b'EDIT ');command(a,'blocked-editor',b'CHAT ERROR target');command(b,'/CANCEL',b'CANCELLED')
            command(a,'T '+bid,b'CHAT TALK OPEN ');time.sleep(1.05);command(b,'PASSWD',b'CURRENT PASSWORD');command(a,'blocked-password',b'CHAT ERROR target');command(b,'/CANCEL',b'CANCELLED')
            command(a,'T '+bid,b'CHAT TALK OPEN ');time.sleep(.55);command(a,'after-busy',b'CHAT PRIVATE TO ');until(b,b'CHAT PRIVATE FROM ')
            command(b,'LOGOUT',b'LOGGED OUT');until(a,b'CHAT TALK GONE');command(a,'no-fallback',b'CHAT ERROR target')
            command(a,'//',b'TALK CLOSED');command(a,'PING',b'PONG')
        finally:a.close();b.close();c.close();s.close()
        s=Server(binary,p/'bbs',members=p/'members',menu=True,telnet=True,language='en');a,b=Telnet(s.port),Telnet(s.port)
        try:
            ids=[]
            for x,name in [(a,'alice'),(b,'bob')]:
                initial=x.until();ids.append(initial.split(b'RTBBS-PROBE ')[1].split()[0]);x.cmd('LOGIN '+name,b'> ');x.cmd(PASSWORD);x.cmd('LANG en')
            a.cmd('TALK '+ids[1].decode(),b'Talk #'+ids[1]+b'> ')
            b.send(b'PING');b.until(b'PING');a.cmd('notice',b'Talk #'+ids[1]+b'> ')
            assert b'Private from' in b.until(b'PING');b.send(b'\r\n');response=b.until();assert b'PONG' in response,response
            a.cmd('//');a.cmd('PING')
        finally:a.close();b.close();s.close()
    print('PASS: outside-room talk, online roster, reply/return, no public room leak, busy editor/password rejection, logout stale-target blocking and Telnet draft redraw')
if __name__=='__main__':run(sys.argv[1])
