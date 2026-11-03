"""NAWS -> live PTY acceptance using real Telnet, Python and an independent stty app."""
from pathlib import Path
import os
import re
import struct
import sys
import tempfile
import time
from test_board import Server
from test_telnet import Client


def naws_frame(columns, rows):
    body=struct.pack('!HH',columns,rows).replace(b'\xff',b'\xff\xff')
    return b'\xff\xfa\x1f'+body+b'\xff\xf0'


def naws(client, columns, rows):
    client.send(naws_frame(columns,rows))


def size(client, expected):
    output=client.cmd('size',b'DOOR> ')
    matches=re.findall(rb'SIZE (\d+) (\d+) SIGNALS (\d+)',output)
    assert matches,output
    cols,rows,signals=map(int,matches[-1]);assert (cols,rows)==expected,(expected,output)
    return signals


def gone(pid):
    for _ in range(50):
        try:os.kill(pid,0)
        except ProcessLookupError:return
        time.sleep(.02)
    raise AssertionError(('Door survived termination',pid))


def run(binary):
    binary=str(Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='ln-door-resize-') as directory:
        root=Path(directory);game=root/'size-door'
        game.write_text('#!'+sys.executable+'''\nimport os,signal,sys
signals=0
def resized(signum,frame):
    global signals
    signals+=1
signal.signal(signal.SIGWINCH,resized)
print('IDENTITY '+os.environ['LINER_USER_ID']+':'+os.environ['LINER_USER'],flush=True)
print('ENV '+os.environ['COLUMNS']+' '+os.environ['LINES'],flush=True)
print('PID '+str(os.getpid()),flush=True)
print('DOOR> ',end='',flush=True)
for line in sys.stdin:
    command=line.strip()
    if command=='quit':break
    if command=='crash':os._exit(42)
    cols,rows=os.get_terminal_size(0)
    print('SIZE %d %d SIGNALS %d'%(cols,rows,signals),flush=True)
    print('DOOR> ',end='',flush=True)
''');game.chmod(0o700)
        shell=root/'stty-door'
        shell.write_text('#!/bin/sh\nprintf "STTY> "\nwhile IFS= read -r line; do\n [ "$line" = quit ] && exit 0\n printf "STTY_SIZE "\n stty size\n printf "STTY> "\ndone\n');shell.chmod(0o700)
        config=root/'doors.conf';config.write_text(f'1|0|30|{game}|{root}|Resize probe\n2|0|30|{shell}|{root}|Independent stty\n3|0|1|{game}|{root}|Timeout probe\n')
        server=Server(binary,root/'notes',members=root/'members',telnet=True,menu=True,language='en',options=['--doors',str(config)])
        clients=[]
        def login(name):
            c=Client(server.port);clients.append(c);c.until()
            c.send(('REGISTER '+name+'\r\nresize-test-password\r\n').encode())
            assert ('Logged in as '+name).encode() in c.until()
            return c
        def start(c,number=1):
            result=c.cmd('DOOR '+str(number),b'DOOR> ')
            assert b'IDENTITY ' in result,result
            return int(re.search(rb'PID (\d+)',result)[1]),result
        def returned(c):
            output=c.until();assert b'Back to BBS' in output,output
            assert b'PONG' in c.cmd('PING')
        try:
            a=login('alice');b=login('bob')
            output=a.cmd('PAGER SIZE 90 28');assert b'ERR ' not in output,output
            pid,output=start(a);assert b'ENV 90 28' in output and b'IDENTITY 1:alice' in output,output
            assert size(a,(90,28))==0  # no NAWS: preserve the user's existing size
            naws(a,80,24);first=size(a,(80,24));assert first>=1
            naws(a,100,30);second=size(a,(100,30));assert second>first
            for _ in range(3):naws(a,100,30)
            assert size(a,(100,30))==second
            # Real split SB packet: do not apply until IAC SE is complete.
            a.send(b'\xff\xfa\x1f\x00\x78')
            assert b'PONG' in b.cmd('PING')
            a.send(b'\x00\x28\xff\xf0');size(a,(120,40))
            # Short/long NAWS payloads are ignored; keep the same PTY size.
            a.send(b'\xff\xfa\x1f\x00\x64\x00\xff\xf0')
            size(a,(120,40))
            a.send(b'\xff\xfa\x1f\x00\x64\x00\x1e\x00\xff\xf0')
            size(a,(120,40))
            for cols,rows,expected in [(20,5,(20,5)),(240,100,(240,100)),(0,30,(80,30)),(100,0,(100,24)),(19,101,(80,24)),(241,4,(80,24)),(65535,65535,(80,24)),(255,50,(80,50)),(80,120,(80,24)),(0,0,(80,24))]:
                naws(a,cols,rows);size(a,expected)
            naws(a,111,33);size(a,(111,33))
            a.send(b'\xff\xfc\x1f');size(a,(80,24))  # WONT NAWS restores defaults
            naws(a,120,40);size(a,(80,24))  # unsolicited NAWS after refusal is ignored
            a.send(b'\xff\xfb\x1f');naws(a,100,30);size(a,(100,30))
            _,other=start(b);assert b'IDENTITY 2:bob' in other
            naws(b,70,18);size(b,(70,18));size(a,(100,30))
            a.send(b'quit\r\n');returned(a);gone(pid)
            pid,output=start(a);assert b'ENV 100 30' in output,output
            naws(a,80,24);size(a,(80,24));a.send(b'crash\r\n');returned(a);gone(pid)
            # Ordinary BBS NAWS remains effective when the next Door starts.
            naws(a,132,43);assert b'PONG' in a.cmd('PING')
            pid,output=start(a);assert b'ENV 132 43' in output,output
            a.send(naws_frame(100,30)+b'\x1d');returned(a);gone(pid)
            pid,output=start(a);assert b'ENV 100 30' in output,output
            naws(a,90,25);size(a,(90,25));a.close();gone(pid)
            size(b,(70,18));b.send(b'quit\r\n');returned(b)
            pid,_=start(b,3);naws(b,100,30);size(b,(100,30));returned(b);gone(pid)
            # A second program uses stty rather than Python's terminal API.
            assert b'STTY> ' in b.cmd('DOOR 2',b'STTY> ')
            naws(b,104,35)
            output=b.cmd('size',b'STTY> ');assert b'STTY_SIZE 35 104' in output,output
            b.send(b'quit\r\n');returned(b)
            print('PASS live Door NAWS: existing 20..240 x 5..100 policy, missing/zero/invalid/duplicate/fragmented/refused notifications, SIGWINCH, identity/environment, independent sessions, quit/reentry/crash/escape/disconnect/timeout, BBS return, independent stty PTY app')
        finally:
            for c in clients:c.close()
            server.close()


if __name__=='__main__':run(sys.argv[1])
