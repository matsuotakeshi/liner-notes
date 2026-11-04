import pathlib
import tempfile
import sys
import time
import subprocess
import concurrent.futures
import hashlib
import os
import signal
import shutil
from test_runtime import Client
from test_board import Server,post,read
from test_notes import page,navigate

PASSWORD='test-only-secret-8721'

def auth(c,name,create=False,password=PASSWORD):
    c.sock.settimeout(10)
    command=f'{"REGISTER" if create else "LOGIN"} {name}\n'.encode()
    c.send(command);reply=c.line()
    # Re-login after fast operations may still be inside the documented
    # one-second credential throttle. Raw throttle tests do not use auth().
    if reply==b'ERR try later':
        time.sleep(1.05);c.send(command);reply=c.line()
    assert reply==b'PASSWORD',reply
    c.send((password+'\n').encode())
    c.expect(f'LOGGED IN {name}'.encode())

def mark(c,id,seen=True):
    cmd='SEEN' if seen else 'UNSEEN'
    c.send(f'{cmd} {id}\n'.encode()); c.expect(f'{cmd} {id}'.encode())

def unread(c,root=0):
    rows=[]; cursor=0
    while True:
        h,r,cursor=page(c,f'UNREAD {root}'+(f' {cursor}' if cursor else ''))
        assert h.startswith(b'UNREAD '); rows+=r
        if not cursor: return [int(x.split()[0][1:]) for x in rows]

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='members-') as d:
        path=pathlib.Path(d); board=path/'board.dat'; members=path/'members.dat'
        s=Server(binary,board,members=members); a,b=Client(s.port),Client(s.port)
        try:
            a.send(b'POST forbidden\n'); a.expect(b'ERR login required')
            a.send(b'NEW\n'); a.expect(b'ERR login required')
            auth(a,'alice',True); auth(b,'bob',True)
            for i in range(12): assert post(a,f'note {i}','body')==i+1
            assert post(b,'response','body',1)==13
            assert unread(a)==list(range(1,14))
            read(a,10)  # reading alone never acknowledges delivery
            assert 10 in unread(a)
            mark(a,10); assert 9 in unread(a) and 10 not in unread(a)
            assert 10 in unread(b)
            mark(a,1); assert unread(a,1)==[13]
            navigate(a,'NEW 1',13); a.send(b'SEEN\n'); a.expect(b'SEEN 13')
            a.send(b'NEW 1\n'); a.expect(b'NO UNREAD')
            mark(a,1,False); assert unread(a,1)==[1]
            a.send(b'READ 13\n'); assert b'AUTHOR bob' in a.line()
            a.line(); n=int(a.line().split()[1])
            while len(a.buf)<n: a.buf+=a.sock.recv(8192)
            a.buf=a.buf[n:]; a.expect(b'END')
            print('PASS: member-only posting, authors, per-article unread holes, thread filter and explicit acknowledgement')
            a.send(b'LOGOUT\n'); a.expect(b'LOGGED OUT'); a.send(b'SEEN 2\n'); a.expect(b'ERR login required')
            a.close(); a=Client(s.port)
            a.send(b'LOGIN alice\n'); a.expect(b'PASSWORD'); a.send(b'incorrect-pass\n'); a.expect(b'ERR authentication')
            a.send(b'POST forbidden\n'); a.expect(b'ERR login required')
            a.close(); a=Client(s.port); auth(a,'alice')
            assert 10 not in unread(a) and 13 not in unread(a)
            print('PASS: logout, wrong password and reconnect preserve isolation')
            def shared(id):
                c=Client(s.port)
                try: auth(c,'alice'); mark(c,id)
                finally: c.close()
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                list(pool.map(shared,[2,3]))
            assert 2 not in unread(a) and 3 not in unread(a)
            print('PASS: two connections of same account merge read acknowledgements')
        finally: s.close(crash=True); a.close(); b.close()
        assert PASSWORD.encode() not in members.read_bytes()
        account=members.read_bytes()[8:136]
        assert hashlib.pbkdf2_hmac('sha256',PASSWORD.encode(),account[40:56],600000)==account[56:88]
        s=Server(binary,board,members=members); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'bob')
            assert unread(a)==[1,4,5,6,7,8,9,11,12]
            assert unread(b)==list(range(1,14))
        finally: a.close(); b.close(); s.close()
        print('PASS: forced restart restores accounts and independent read sets; no plaintext password stored')
        raw=members.read_bytes(); broken=path/'broken.dat'; broken.write_bytes(raw[:-5])
        p=subprocess.run([binary,'0',str(board),str(broken)],capture_output=True,timeout=10)
        assert p.returncode and b'members:' in p.stderr and broken.read_bytes()==raw[:-5]
        print('PASS: truncated member journal rejected without alteration')
        if os.name=='posix':
            import fault_io as resource
            failed_board=path/'failure-board.dat'; failed_members=path/'failure-members.dat'
            shutil.copyfile(board,failed_board); shutil.copyfile(members,failed_members)
            limit=failed_members.stat().st_size+64
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,failed_board,members=failed_members,preexec_fn=size_limit,options=["--user-log","-"]); c=Client(s.port)
            try:
                auth(c,'alice'); c.send(b'SEEN 1\n'); c.expect(b'ERR read-state storage')
                c.send(b'UNREAD\n'); c.expect(b'ERR member storage unavailable')
                c.send(b'PING\n'); c.expect(b'PONG')
                c.send(b'SEEN 4\n'); c.expect(b'ERR member storage unavailable')
            finally: c.close(); s.close()
            assert failed_members.stat().st_size==limit
            print('PASS: OS short-write in read journal does not acknowledge or mark article seen')

if __name__=='__main__': run(sys.argv[1])
