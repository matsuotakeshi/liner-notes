"""Account lifecycle: persistent suspension, all sessions revoked, safe restore."""
import os
import pathlib
import signal
import struct
import subprocess
import sys
import tempfile
from test_runtime import Client
from test_board import Server,post,read
from test_members import auth,unread,PASSWORD
from test_notes import page
from test_moderation import expect,checksum
from test_ui import Menu
from test_telnet import Client as TelnetClient


def closed(c):
    output=c.buf; c.buf=b''
    try:
        while True:
            data=c.sock.recv(8192)
            if not data: break
            output+=data
    except ConnectionResetError: pass
    assert b'ERR session revoked' in output, output
    return output


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='suspension-') as d:
        p=pathlib.Path(d); board=p/'board'; users=p/'users'; admins=p/'admins'; journal=p/'board.moderation'
        s=Server(binary,board,members=users); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice',True); auth(b,'bob',True)
            assert post(b,'Before suspension','Body retained')==1
            for i in range(3,11):
                c=Client(s.port)
                try: auth(c,f'user{i}',True)
                finally: c.close()
        finally: a.close(); b.close(); s.close()
        admins.write_text('1\n3\n')
        original_board=board.read_bytes(); original_users=users.read_bytes()
        s=Server(binary,board,members=users,admins=admins)
        a,b,c,t,g=Client(s.port),Client(s.port),Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'bob'); auth(c,'bob'); auth(t,'bob')
            for client in (b,g):
                for cmd in ['MEMBERS','MEMBER 2','SUSPEND 2','RESUME 2']:
                    expect(client,cmd,'ERR admin required')
            h,rows,next_id=page(a,'MEMBERS'); assert h==b'MEMBERS' and len(rows)==8 and next_id==9
            _,tail,last=page(a,f'MEMBERS {next_id}'); assert len(tail)==2 and last==0
            expect(a,'MEMBER 2','MEMBER 2 bob active')
            for id in [1,3]:
                expect(a,f'SUSPEND {id}','ERR protected member'); expect(a,f'RESUME {id}','ERR protected member')
            expect(a,'SUSPEND 999','ERR no member'); expect(a,'SUSPEND 1000000','ERR member number')
            expect(b,'POST unfinished','EDIT end with . or /CANCEL'); b.send(b'draft before suspension\n')
            expect(t,'WAIT 5000','WAITING')
            expect(a,'SUSPEND 2','SUSPENDED 2')
            expect(a,'MEMBER 2','MEMBER 2 bob suspended')
            size=journal.stat().st_size; expect(a,'SUSPEND 2','SUSPENDED 2'); assert journal.stat().st_size==size
            # Resume BEFORE the victims read: old sessions must not come back.
            expect(a,'RESUME 2','RESUMED 2')
            for victim in [b,c,t]:
                output=closed(victim); assert b'SAVED' not in output and b'AWAKE' not in output
            b.close(); b=Client(s.port); auth(b,'bob')
            assert read(b,1)[1:] == ('Before suspension','Body retained\r\n') and unread(b)==[1]
            assert post(b,'After resume','Restored access')==2
            expect(a,'SUSPEND 2','SUSPENDED 2'); closed(b)
            g.send(b'LOGIN bob\n'); g.expect(b'PASSWORD'); g.send((PASSWORD+'\n').encode()); g.expect(b'ERR authentication')
            expect(g,'ACCOUNT','ERR login required')
            g.close(); g=Client(s.port)
            expect(g,'REGISTER bob\n'+PASSWORD,'PASSWORD'); g.expect(b'ERR authentication')
            print('PASS: admin-only listing/status/suspend/resume, protected admins, pagination and idempotent operations')
            print('PASS: all logged-in sessions including drafts/timers close; immediate resume does not revive them')
            print('PASS: new login required after resume; suspension does not delete posts or read state')
        finally:
            s.close(crash=True)
            for client in [a,b,c,t,g]: client.close()
        snapshot=board.read_bytes(); assert snapshot.startswith(original_board)
        assert users.read_bytes()==original_users  # Suspension has a separate audit journal.
        s=Server(binary,board,members=users,admins=admins); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); expect(a,'MEMBER 2','MEMBER 2 bob suspended')
            b.send(b'LOGIN bob\n'+PASSWORD.encode()+b'\n'); b.expect(b'PASSWORD'); b.expect(b'ERR authentication')
            expect(a,'RESUME 2','RESUMED 2')
            b.close(); b=Client(s.port); auth(b,'bob'); assert unread(b)==[1,2]
            expect(a,'SUSPEND 2','SUSPENDED 2'); closed(b)
        finally: a.close(); b.close(); s.close()
        print('PASS: forced restart retains suspension; existing password and unread holes survive restore')
        raw=journal.read_bytes(); seen=0
        for offset in range(8,len(raw),32):
            row=raw[offset:offset+32]
            if row[0]==ord('S'):
                seen+=1; assert struct.unpack_from('<I',row,8)[0]==2
                assert struct.unpack_from('<I',row,12)[0]==0 and struct.unpack_from('<I',row,16)[0]==1
                assert struct.unpack_from('<Q',row,20)[0]>0 and row[28:]==checksum(row)
        assert seen==5
        # A new admin config cannot silently elevate a still-suspended account.
        admins.write_text('1\n2\n')
        result=subprocess.run([binary,'--admins',str(admins),'0',str(board),str(users)],capture_output=True,timeout=10)
        assert result.returncode and b'moderation:' in result.stderr and journal.read_bytes()==raw
        admins.write_text('1\n3\n')
        for pos,value in [(8,999),(12,1),(16,2)]:
            bad=bytearray(raw); row=bytearray(bad[-32:]); struct.pack_into('<I',row,pos,value); row[28:]=checksum(row); bad[-32:]=row
            journal.write_bytes(bad)
            result=subprocess.run([binary,'--admins',str(admins),'0',str(board),str(users)],capture_output=True,timeout=10)
            assert result.returncode and b'moderation:' in result.stderr and journal.read_bytes()==bad
        journal.write_bytes(raw)
        print('PASS: account audit actor/time/target validated; malformed records and suspended-admin config rejected unchanged')
        # Menu and Telnet use the same session revocation path.
        s=Server(binary,board,members=users,admins=admins,menu=True,telnet=True,language='en')
        a,b=TelnetClient(s.port),TelnetClient(s.port)
        try:
            a.until(); b.until(); a.cmd('LOGIN alice',b'> '); a.cmd(PASSWORD)
            assert b'Members' in a.cmd('MEMBERS'); assert b'user9' in a.cmd('+')
            assert b'Active' in a.cmd('9')
            a.cmd('RESUME 2'); b.cmd('LOGIN bob',b'> '); b.cmd(PASSWORD)
            a.cmd('LANG ja'); assert '利用停止'.encode() in a.cmd('SUSPEND 2')
            assert b'connection is closing' in b.until(b'connection is closing')
            assert '利用停止'.encode() in a.cmd('MEMBER 2')
            assert '解除'.encode() in a.cmd('RESUME 2')
        finally: a.close(); b.close(); s.close()
        print('PASS: English/Japanese member management and Telnet victim notification/closure')
        if os.name=='posix':
            original_users=users.read_bytes()  # after the earlier UI preference saves
            import fault_io as resource
            limit=journal.stat().st_size+16
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,board,members=users,admins=admins,preexec_fn=size_limit,options=["--user-log","-"]); a,b,g=Client(s.port),Client(s.port),Client(s.port)
            try:
                auth(a,'alice'); auth(b,'bob')
                expect(a,'SUSPEND 2','ERR moderation storage uncertain')
                expect(b,'READ 1','ERR moderation storage unavailable')
                g.send(b'LOGIN bob\n'+PASSWORD.encode()+b'\n'); g.expect(b'PASSWORD'); g.expect(b'ERR moderation storage unavailable')
                expect(b,'PING','PONG')
            finally: a.close(); b.close(); g.close(); s.close()
            assert journal.stat().st_size==limit and board.read_bytes()==snapshot and users.read_bytes()==original_users
            result=subprocess.run([binary,'0',str(board),str(users)],capture_output=True,timeout=10)
            assert result.returncode and b'moderation:' in result.stderr
            print('PASS: OS short-write cannot acknowledge suspension; board access and new logins fail closed, corrupt restart refused')


if __name__=='__main__': run(sys.argv[1])
