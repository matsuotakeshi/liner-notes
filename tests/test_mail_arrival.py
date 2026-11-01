"""Live private mail notifications and safe Telnet prompt restoration."""
import pathlib,sys,tempfile,time,select
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_mail import send as raw_send
from test_talk import command
from test_telnet import Client as Telnet
EN=b'new mail message(s) arrived'
JA='新着メールが'.encode()
MP=b'0 exit)> '

def quiet(c):
    end=time.monotonic()+.25
    while time.monotonic()<end:
        if select.select([c.sock],[],[],max(0,end-time.monotonic()))[0]:c.receive()
    assert EN not in c.buf and JA not in c.buf,c.buf

def send(c,target='bob'):
    out=c.cmd('MAIL SEND '+target+' secret-title',b'Body > ')
    c.cmd('secret-body',b'Body > ')
    assert b'saved and sent' in c.cmd('.',MP)

def notice(c,marker=EN):
    out=c.until(marker);out+=c.until();return out

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='arrival-') as d:
        p=pathlib.Path(d);data=p/'bbs';members=p/'members'
        s=Server(binary,data,members=members);a,b,c=[Client(s.port) for _ in range(3)]
        try:
            for x,name in [(a,'alice'),(b,'bob'),(c,'carol')]:auth(x,name,True)
            raw_send(a,'bob','raw','body');command(b,'PING',b'PONG')
        finally:a.close();b.close();c.close();s.close()
        s=Server(binary,data,members=members,menu=True,telnet=True,language='en');a,b,b2,c=[Telnet(s.port) for _ in range(4)]
        try:
            for x,name in [(a,'alice'),(b,'bob'),(b2,'bob'),(c,'carol')]:x.until();x.cmd('LOGIN '+name,b'> ');x.cmd(PASSWORD)
            b.send(b'PI');b.until(b'PI');send(a)
            out=b.until(b'] > PI');assert EN in out and b'secret-title' not in out and b'secret-body' not in out
            assert b'PONG' in b.cmd('NG');assert EN in notice(b2);quiet(c);quiet(b)
            b.cmd('POST draft',b'Body > ');send(a);notice(b2);send(a);notice(b2);quiet(b)
            b.cmd('/CANCEL');out=notice(b);assert b'2 new mail' in out
            time.sleep(1.05);b.cmd('PASSWD',b'> ');send(a);notice(b2);quiet(b)
            b.cmd('/CANCEL');notice(b)
            b.cmd('LANG ja');send(a);assert JA in notice(b,JA);notice(b2)
            # Failed delivery produces no arrival notification.
            assert b'unavailable' in a.cmd('MAIL SEND missing fail',MP);quiet(b);quiet(b2)
            b.cmd('LOGOUT');send(a);notice(b2);quiet(b);quiet(c)
        finally:
            for x in [a,b,b2,c]:x.close()
            s.close()
    print('PASS: committed live arrivals on all recipient connections, no third-party/content leaks, partial input redraw, deferred editor/password aggregation, Japanese/English, failed send/logout/raw isolation')
if __name__=='__main__':run(sys.argv[1])
