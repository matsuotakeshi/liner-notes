"""Persistent per-member arrival settings, multi-session propagation and journal failures."""
import pathlib,sys,tempfile,subprocess,struct,os,signal
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_talk import command
from test_mail_arrival import send,notice,quiet,EN,JA,MP
from test_telnet import Client as Telnet

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='mailnotify-') as d:
        p=pathlib.Path(d);data=p/'bbs';members=p/'members'
        s=Server(binary,data,members=members);a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice',True);auth(b,'bob',True);command(g,'MAIL NOTIFY OFF',b'ERR login required')
            before=members.stat().st_size;command(b,'MAIL NOTIFY',b'MAIL NOTIFY ON');command(b,'MAIL NOTIFY ON',b'MAIL NOTIFY ON');assert members.stat().st_size==before
            command(b,'MAIL NOTIFY OFF',b'MAIL NOTIFY OFF');assert members.stat().st_size==before+128
            before=members.stat().st_size;command(b,'MAIL NOTIFY OFF',b'MAIL NOTIFY OFF');assert members.stat().st_size==before
            command(a,'MAIL NOTIFY',b'MAIL NOTIFY ON')
        finally:a.close();b.close();g.close();s.close()
        s=Server(binary,data,members=members,menu=True,telnet=True,language='en');a,b,b2=[Telnet(s.port) for _ in range(3)]
        try:
            for x,name in [(a,'alice'),(b,'bob'),(b2,'bob')]:x.until();x.cmd('LOGIN '+name,b'> ');x.cmd(PASSWORD)
            assert b'OFF' in b.cmd('MAIL NOTIFY',MP);b.cmd('0');send(a);quiet(b);quiet(b2)
            assert b'ON' in b2.cmd('MAIL NOTIFY ON',MP);b2.cmd('0');quiet(b);quiet(b2)
            send(a);notice(b);notice(b2)
            b.cmd('POST draft',b'Body > ');send(a);notice(b2);quiet(b)
            assert b'OFF' in b2.cmd('MAIL NOTIFY OFF',MP);b2.cmd('0');b.cmd('/CANCEL');quiet(b)
            b2.cmd('LANG ja');assert '新着メール通知: ON'.encode() in b2.cmd('MAIL NOTIFY ON','0終了)> '.encode());b2.cmd('0');quiet(b);quiet(b2)
            send(a);notice(b);notice(b2,JA)
            b2.cmd('MAIL NOTIFY OFF','0終了)> '.encode());b2.cmd('0')
        finally:a.close();b.close();b2.close();s.close()
        s=Server(binary,data,members=members);b=Client(s.port)
        try:
            auth(b,'bob');command(b,'MAIL NOTIFY',b'MAIL NOTIFY OFF')
            command(b,'0',b'MAIL CLOSED');command(b,'PREFS SAVE',b'PREFS SAVED');command(b,'MAIL NOTIFY',b'MAIL NOTIFY OFF')
        finally:b.close();s.close()
        raw=members.read_bytes();bad=bytearray(raw);offset=next(i for i in range(8,len(raw),128) if raw[i]==ord('N'));bad[offset+8]=2
        h=2166136261
        for v in bad[offset:offset+124]:h=((h^v)*16777619)&0xffffffff
        struct.pack_into('<I',bad,offset+124,h)
        for damaged in [bytes(bad),raw[:offset+127]]:
            members.write_bytes(damaged);r=subprocess.run([binary,'0',str(data),str(members)],capture_output=True,timeout=10)
            assert r.returncode and members.read_bytes()==damaged
        members.write_bytes(raw)
        if os.name=='posix':
            import fault_io as resource
            limit=len(raw)+64
            def size_limit():signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,data,members=members,preexec_fn=size_limit,options=["--user-log","-"]);b=Client(s.port)
            try:auth(b,'bob');command(b,'MAIL NOTIFY ON',b'ERR member storage unavailable')
            finally:b.close();s.close()
            assert members.stat().st_size==limit
            r=subprocess.run([binary,'0',str(data),str(members)],capture_output=True,timeout=10);assert r.returncode
    print('PASS: default ON, owner-only persistent ON/OFF/query, idempotent writes, live peer updates, pending-notice cancellation, no replay on ON, restart, language/preferences isolation, malformed/truncated journal rejection')
if __name__=='__main__':run(sys.argv[1])
