"""Real Telnet, private dropfiles, cross-runtime slots and process-group cleanup."""
from pathlib import Path
import os, re, sys, tempfile, time
from test_board import Server
from test_telnet import Client
from test_door_resize import gone

PROMPT=b'0:main] > '

def run(binary):
    binary=str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='ln-generic-doors-') as tmp:
        p=Path(tmp);game=p/'probe';other=p/'other';other.mkdir()
        game.write_text('#!'+sys.executable+'''
import os,sys,tty,json,signal
from pathlib import Path
tty.setraw(0)
drop=Path(os.environ['LINER_DOOR_DROP_DIR'])
info={'pid':os.getpid(),'node':os.environ['LINER_DOOR_NODE'],'drop':str(drop),'user':os.environ['LINER_USER_ID']}
print('PROBE '+json.dumps(info)+'\\r',flush=True)
print('KEY> ',end='',flush=True)
while True:
    c=os.read(0,1)
    if c==b'q':break
    if c==b'x':os._exit(42)
    if c==b'i':signal.signal(signal.SIGHUP,signal.SIG_IGN)
    if c==b'f':
        child=os.fork()
        if not child:
            import time
            while True:time.sleep(1)
        print('CHILD '+str(child)+'\\r',flush=True)
    print('KEY '+c.hex()+'\\r\\nKEY> ',end='',flush=True)
''');game.chmod(0o700)
        conf=p/'doors';conf.write_text(f'1|0|30|{game}|{p}|Probe|dorinfo1:1\n2|5|30|{game}|{p}|Restricted|dorinfo1:1\n3|0|30|{game}|{other}|Other|dorinfo1:2\n4|0|1|{game}|{other}|Timed|dorinfo1:2\n')
        servers=[];clients=[]
        def server(label):
            s=Server(binary,p/(label+'.dat'),members=p/(label+'.members'),menu=True,telnet=True,language='en',options=['--doors',str(conf)]);servers.append(s);return s
        def login(s,name):
            c=Client(s.port);c.door_test_identity=name;clients.append(c);c.until();c.send(('REGISTER '+name+'\r\nprivate-password\r\n').encode());assert b'Logged in' in c.until();return c
        def start(c,n):
            import json
            print(f"Door start: user={c.door_test_identity} game={n}",flush=True)
            try:
                out=c.cmd(str(n),b'KEY> ')
            except (TimeoutError, AssertionError) as error:
                states=[{'pid':server.proc.pid,'returncode':server.proc.poll()} for server in servers]
                raise AssertionError(f'Door start failed: user={c.door_test_identity} game={n} '
                                     f'received={c.buf!r} runtimes={states}') from error
            return json.loads(re.search(rb'PROBE (\{[^\r\n]+\})',out)[1])
        def finish(c,key=b'q'):
            c.send(key);out=c.until(PROMPT);assert b'Back to BBS' in out and b'1: Probe' in out,out
        try:
            s=server('one');s2=server('two')
            a=login(s,'alice');b=login(s,'bob');c=login(s2,'carol')
            guest=Client(s.port);clients.append(guest);guest.until();assert b'login' in guest.cmd('GAME').lower()
            for client in (a,b,c):
                out=client.cmd('game',PROMPT);assert b'1: Probe' in out and b'2: Restricted' not in out,out
            info=start(a,1);drop=Path(info['drop']);data=(drop/'DORINFO1.DEF').read_bytes()
            assert data.split(b'\r\n')==[b'Liner notes',b'SYSOP',b'',b'COM0',b'0 BAUD,N,8,1',b'0',b'LN000000001',b'',b'',b'1',b'2',b'1',b''],data
            assert drop.stat().st_mode&0o777==0o700 and (drop/'DORINFO1.DEF').stat().st_mode&0o777==0o600
            assert b'alice' not in data and b'password' not in data
            for client in (b,c):
                out=client.cmd('1',PROMPT);assert b'KEY> ' not in out and b'game' in out.lower(),out
            info2=start(b,3);info3=start(c,3)
            assert info2['node']!=info3['node'] and info2['drop']!=info3['drop']
            a.send(b'\x1b');assert b'KEY 1b' in a.until(b'KEY> ') # immediate key; no Enter
            finish(a);gone(info['pid'])
            for _ in range(50):
                if not drop.exists():break
                time.sleep(.02)
            assert not drop.exists()
            finish(b,b'x');finish(c)
            info=start(a,1);a.send(b'i');a.until(b'KEY> ');a.send(b'f');out=a.until(b'KEY> ');child=int(re.search(rb'CHILD (\d+)',out)[1]);a.close();clients.remove(a)
            gone(info['pid']);gone(child);time.sleep(.1)
            info=start(b,1);finish(b,b'\x1d');gone(info['pid'])
            info=start(c,4);out=c.until(PROMPT);assert b'1: Probe' in out;gone(info['pid'])
            # Repeated list/invalid choices cannot become article reads.
            assert b'1: Probe' in b.cmd('',PROMPT)
            assert b'KEY> ' not in b.cmd('999',PROMPT)
            assert b'game number' in b.cmd('POST accidental',PROMPT)
            assert b'MAIN' in b.cmd('0')
            assert b'PONG' in b.cmd('PING')
            # A preexisting symlink must never be used as a drop/slot file.
            (other/'.liner-door-slot-1').unlink();(other/'.liner-door-slot-1').symlink_to(p/'target')
            out=c.cmd('3',PROMPT);assert b'KEY> ' not in out and not (p/'target').exists(),out
        finally:
            for client in clients:client.close()
            for s in servers:s.close()
    print('generic Door: menu, raw keys, private DORINFO, slots across runtimes, timeout/crash/escape/disconnect and descendants passed')

if __name__=='__main__':run(sys.argv[1])
