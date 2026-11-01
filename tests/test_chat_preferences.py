"""Durable chat controls, shared account state and journal failure behavior."""
import os,pathlib,signal,struct,subprocess,sys,tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_chat import join,until
from test_talk import command,roster
from test_chat_controls import settings

def connect(s,name,create=False):
    c=Client(s.port);auth(c,name,create);join(c);return c

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='chat-preferences-') as d:
        p=pathlib.Path(d);data=p/'bbs';users=p/'members'
        s=Server(binary,data,members=users);clients=[]
        try:
            a=connect(s,'alice',True);b=connect(s,'bob',True);c=connect(s,'carol',True);b2=connect(s,'bob');clients=[a,b,c,b2]
            bid=b.greeting.split()[1].decode();b2id=b2.greeting.split()[1].decode()
            for x in clients:roster(x)
            settings(b,'/MUTE alice');assert b'CHAT MUTED alice' in settings(b2,'/MUTES')
            settings(b2,'/MUTE carol');v=settings(b,'/MUTES');assert b'CHAT MUTED alice' in v and b'CHAT MUTED carol' in v
            settings(b,'/TALK OFF');assert b'CHAT RECEIVE OFF' in settings(b2,'/MUTES')
            # Duplicate operations must not append to the account journal.
            size=users.stat().st_size;settings(b2,'/TALK OFF');settings(b2,'/MUTE alice');assert users.stat().st_size==size
            command(a,'T '+b2id,b'CHAT TALK OPEN ');command(a,'blocked-peer',b'CHAT ERROR refused');roster(b2,b'blocked-peer')
            # Changes from either connection are incremental, not stale whole-list replacements.
            settings(b2,'/UNMUTE carol');v=settings(b,'/MUTES');assert b'CHAT MUTED alice' in v and b'CHAT MUTED carol' not in v
            settings(b2,'/TALK ON');command(a,'still-muted',b'CHAT ERROR refused')
            command(a,'//',b'CHAT TALK CLOSED');command(a,'history-must-stay-hidden',b'CHAT SAY ')
            settings(b,'/TALK OFF')
        finally:
            for x in clients:x.close()
            s.close()
        original=users.read_bytes();board=data.read_bytes()
        s=Server(binary,data,members=users);clients=[]
        try:
            a=connect(s,'alice');b=connect(s,'bob');clients=[a,b]
            v=settings(b,'/MUTES');assert b'CHAT RECEIVE OFF' in v and b'CHAT MUTED alice' in v
            command(a,'persisted-public',b'CHAT SAY ');roster(b,b'persisted-public')
            command(b,'/LEAVE',b'CHAT CLOSED');b.send(b'CHAT\n');rows=[]
            while True:
                line=b.line();rows.append(line)
                assert b'persisted-public' not in line
                if line.startswith(b'CHAT JOIN ') and b' bob\t' in line:break
            # Offline registered users are valid mute targets.
            settings(b,'/MUTE carol');settings(b,'/UNMUTE carol')
            settings(b,'/TALK ON');settings(b,'/UNMUTE alice')
        finally:
            for x in clients:x.close()
            s.close()
        s=Server(binary,data,members=users);b=connect(s,'bob')
        try:
            v=settings(b,'/MUTES');assert b'CHAT RECEIVE ON' in v and b'CHAT MUTED' not in v
        finally:b.close();s.close()
        raw=users.read_bytes();assert data.read_bytes()==board
        # Valid-checksum malformed records are rejected without rewriting the file.
        for kind,offset,value in [('C',8,2),('C',9,1),('M',12,0),('M',12,2),('M',12,999),('M',16,1),('M',4,999)]:
            row=bytearray(next(original[i:i+128] for i in range(8,len(original),128) if original[i]==ord(kind)))
            if offset in (4,12):struct.pack_into('<I',row,offset,value)
            else:row[offset]=value
            h=2166136261
            for byte in row[:124]:h=((h^byte)*16777619)&0xffffffff
            struct.pack_into('<I',row,124,h)
            bad=raw+row;users.write_bytes(bad)
            r=subprocess.run([binary,'0',str(data),str(users)],capture_output=True,timeout=10)
            assert r.returncode and users.read_bytes()==bad
        users.write_bytes(raw[:-1]);r=subprocess.run([binary,'0',str(data),str(users)],capture_output=True,timeout=10)
        assert r.returncode and users.read_bytes()==raw[:-1];users.write_bytes(raw)
        if os.name=='posix':
            import fault_io as resource
            limit=len(raw)+64
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,data,members=users,preexec_fn=size_limit,options=["--user-log","-"]);b=connect(s,'bob')
            try:command(b,'/TALK OFF',b'ERR member storage unavailable')
            finally:b.close();s.close()
            assert users.stat().st_size==limit
            r=subprocess.run([binary,'0',str(data),str(users)],capture_output=True,timeout=10)
            assert r.returncode and users.stat().st_size==limit
            print('PASS: short write does not acknowledge saved settings; damaged restart rejected unchanged')
        print('PASS: durable receive/mute settings and removal, multi-session incremental updates, duplicate suppression, history filtering on rejoin, offline targets, malformed/truncated journal rejection')

if __name__=='__main__':run(sys.argv[1])
