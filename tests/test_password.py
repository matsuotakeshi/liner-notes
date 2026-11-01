"""Password changes through real sessions, durable storage and secret Telnet input."""
import os
import pathlib
import signal
import subprocess
import sys
import tempfile
import time
from test_runtime import Client
from test_board import Server, post, read
from test_members import auth, mark, unread, PASSWORD
from test_moderation import expect
from test_telnet import Client as TelnetClient

NEW='replacement-secret-9382'


def change(c,old,new=NEW,confirm=None,result='PASSWORD CHANGED'):
    time.sleep(1.05)
    expect(c,'PASSWD','CURRENT PASSWORD')
    expect(c,old,'NEW PASSWORD')
    expect(c,new,'CONFIRM PASSWORD')
    expect(c,new if confirm is None else confirm,result)


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='password-') as d:
        p=pathlib.Path(d); board=p/'board'; users=p/'users'
        s=Server(binary,board,members=users); a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            expect(g,'PASSWD','ERR login required')
            auth(a,'alice',True); auth(b,'alice')
            assert post(a,'kept note','kept body')==1; mark(a,1)
            change(a,'incorrect-current',result='ERR password change')
            expect(b,'ACCOUNT','ACCOUNT 1 alice')
            change(a,PASSWORD,confirm='different-confirmation',result='ERR password mismatch')
            time.sleep(1.05); expect(a,'PASSWD','CURRENT PASSWORD'); expect(a,PASSWORD,'NEW PASSWORD')
            expect(a,PASSWORD,'ERR password unchanged')
            for mode in range(3):
                time.sleep(1.05); expect(a,'PASSWD','CURRENT PASSWORD')
                if mode>=1: expect(a,PASSWORD,'NEW PASSWORD')
                if mode>=2: expect(a,NEW,'CONFIRM PASSWORD')
                expect(a,'/CANCEL','CANCELLED')
            time.sleep(1.05); expect(a,'PASSWD','CURRENT PASSWORD'); expect(a,'short','ERR password 8..128 bytes')
            # Another session in a draft is revoked after the commit.
            expect(b,'POST unfinished','EDIT end with . or /CANCEL')
            change(a,PASSWORD)
            b.expect(b'ERR session revoked'); assert b.sock.recv(1)==b''
            expect(a,'ACCOUNT','ACCOUNT 1 alice'); assert unread(a)==[]
            assert read(a,1)[2]=='kept body\r\n'
            g.send(b'LOGIN alice\n'+PASSWORD.encode()+b'\n'); g.expect(b'PASSWORD'); g.expect(b'ERR authentication')
            g.close(); g=Client(s.port); auth(g,'alice',password=NEW)
        finally: a.close(); b.close(); g.close(); s.close()
        raw=users.read_bytes(); source=board.read_bytes()
        assert PASSWORD.encode() not in raw and NEW.encode() not in raw
        records=[raw[n:n+128] for n in range(8,len(raw),128)]
        assert sum(row[0]==ord('P') for row in records)==1
        assert records[-1][40:56]!=records[0][40:56]
        print('PASS: current-password check, confirmation, cancel at every step, length and unchanged rejection, one durable salted change')
        print('PASS: peer draft revoked, initiating session remains valid, old login rejected, new login works, posts/read state preserved')
        s=Server(binary,board,members=users); a=Client(s.port)
        try:
            auth(a,'alice',password=NEW); assert unread(a)==[]
            change(a,NEW,'x'*128)
        finally: a.close(); s.close()
        print('PASS: restart replays credential replacement; 128-byte password accepted')
        # Real Telnet negotiation and echo suppression for all three input stages.
        s=Server(binary,board,members=users,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('LOGIN alice',b'> '); a.cmd('x'*128); time.sleep(1.05)
            assert b'current password' in a.cmd('PASSWD',b'> ')
            out=a.cmd('x'*128,b'> '); assert b'x'*128 not in out and b'new password' in out
            out=a.cmd(NEW,b'> '); assert NEW.encode() not in out and b'again' in out
            out=a.cmd(NEW); assert NEW.encode() not in out and b'Password changed' in out
            a.cmd('LANG ja'); time.sleep(1.05)
            assert '現在のパスワード'.encode() in a.cmd('PASSWD',b'> ')
            assert '新しいパスワード'.encode() in a.cmd(NEW,b'> ')
            a.cmd(PASSWORD,b'> ')
            assert 'パスワードを変更しました'.encode() in a.cmd(PASSWORD)
        finally: a.close(); s.close()
        assert board.read_bytes()==source
        raw=users.read_bytes()
        print('PASS: Japanese/English Telnet prompts and secret input, repeated changes preserve journal and article data')
        users.write_bytes(raw[:-1])
        result=subprocess.run([binary,'0',str(board),str(users)],capture_output=True,timeout=10)
        assert result.returncode and users.read_bytes()==raw[:-1]
        users.write_bytes(raw)
        if os.name=='posix':
            import fault_io as resource
            limit=len(raw)+64
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,board,members=users,preexec_fn=size_limit,options=["--user-log","-"]); a,g=Client(s.port),Client(s.port)
            try:
                auth(a,'alice'); change(a,PASSWORD,result='ERR member storage unavailable')
                expect(a,'READ 1','ERR member storage unavailable')
                g.send(b'LOGIN alice\n'+PASSWORD.encode()+b'\n'); g.expect(b'PASSWORD'); g.expect(b'ERR member storage unavailable')
                expect(a,'PING','PONG')
            finally: a.close(); g.close(); s.close()
            assert users.stat().st_size==limit and board.read_bytes()==source
            result=subprocess.run([binary,'0',str(board),str(users)],capture_output=True,timeout=10)
            assert result.returncode and users.stat().st_size==limit
            print('PASS: OS short-write never acknowledges change; further board/auth access fails closed; truncated restart refused')
        print('PASS: truncated password journal rejected without rewriting it')


if __name__=='__main__': run(sys.argv[1])
