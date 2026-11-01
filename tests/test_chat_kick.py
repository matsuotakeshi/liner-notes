"""Admin chat removal, scoped connections, durable audit and failure paths."""
import os,pathlib,signal,struct,subprocess,sys,tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_chat import join,until
from test_talk import command,roster
from test_audit import history
from test_chat_controls import settings
from test_telnet import Client as Telnet

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='chat-kick-') as d:
        p=pathlib.Path(d);data=p/'bbs';users=p/'members';admins=p/'admins';journal=p/'bbs.moderation'
        s=Server(binary,data,members=users)
        try:
            for name in ('alice','bob','carol'):
                c=Client(s.port);auth(c,name,True);c.close()
        finally:s.close()
        admins.write_text('1\n3\n');args=dict(members=users,admins=admins)
        s=Server(binary,data,**args);clients=[Client(s.port) for _ in range(5)];a,b,b2,bbs,c=clients
        try:
            for x,name in [(a,'alice'),(b,'bob'),(b2,'bob'),(bbs,'bob'),(c,'carol')]:auth(x,name)
            for x in (a,b,b2,c):join(x)
            for x in (a,b,b2,c):roster(x)
            command(b,'/KICK alice',b'ERR admin required');command(a,'/KICK alice',b'ERR protected member')
            command(a,'/KICK carol',b'ERR protected member');command(a,'/KICK nobody',b'ERR member not found')
            assert journal.stat().st_size==8
            command(c,'T '+b.greeting.split()[1].decode(),b'CHAT TALK OPEN ')
            settings(b,'/TALK OFF');settings(b,'/MUTE alice')
            b2.send(b'unfinished-chat-draft')
            command(a,'/KICK bob',b'CHAT KICK bob 2')
            for x in (b,b2):
                until(x,b'CHAT KICKED');assert x.sock.recv(1)==b''
            until(c,b'CHAT TALK GONE');command(c,'no-public-fallback',b'CHAT ERROR target')
            command(bbs,'PING',b'PONG')
            # Account stays active and can re-enter; preferences survive removal.
            fresh=Client(s.port);clients.append(fresh);auth(fresh,'bob');join(fresh)
            v=settings(fresh,'/MUTES');assert b'CHAT RECEIVE OFF' in v and b'CHAT MUTED alice' in v
            command(a,'/LEAVE',b'CHAT CLOSED')
            entries,_=history(a,'AUDIT ACTOR alice');assert len(entries)==1 and entries[0][4:] == ('CHAT_KICK',2,0)
            entries,_=history(a,'AUDIT ARTICLE 1');assert not entries
        finally:
            for x in clients:x.close()
            s.close()
        raw=journal.read_bytes()
        s=Server(binary,data,**args);a=Client(s.port)
        try:
            auth(a,'alice');entries,_=history(a);assert entries[0][4]=='CHAT_KICK'
        finally:a.close();s.close()
        for offset,value in [(1,0),(8,0),(8,1),(12,1),(16,0)]:
            bad=bytearray(raw);row=bytearray(bad[-32:])
            if offset==1:row[offset]=value
            else:struct.pack_into('<I',row,offset,value)
            h=2166136261
            for byte in row[:28]:h=((h^byte)*16777619)&0xffffffff
            struct.pack_into('<I',row,28,h);bad[-32:]=row;journal.write_bytes(bad)
            r=subprocess.run([binary,'--admins',str(admins),'0',str(data),str(users)],capture_output=True,timeout=10)
            assert r.returncode and journal.read_bytes()==bad
        journal.write_bytes(raw)
        # Actual partial-write on FreeBSD: no removal without committed audit.
        if os.name=='posix':
            import fault_io as resource
            # Grow moderation journal beyond the other files before limiting writes.
            s=Server(binary,data,**args);a=Client(s.port);auth(a,'alice');join(a)
            try:
                for _ in range(40):command(a,'/KICK bob',b'CHAT KICK bob 0')
            finally:a.close();s.close()
            limit=journal.stat().st_size+16
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,data,preexec_fn=size_limit,options=["--user-log","-"],**args);a,b=Client(s.port),Client(s.port)
            try:
                auth(a,'alice');auth(b,'bob');join(a);join(b)
                command(a,'/KICK bob',b'ERR moderation storage uncertain')
                b.send(b'/LEAVE\n');until(b,b'CHAT CLOSED');command(b,'PING',b'PONG')
            finally:a.close();b.close();s.close()
            assert journal.stat().st_size==limit
            r=subprocess.run([binary,'--admins',str(admins),'0',str(data),str(users)],capture_output=True,timeout=10)
            assert r.returncode and journal.stat().st_size==limit
            journal.write_bytes(raw)
        s=Server(binary,data,menu=True,telnet=True,language='en',**args);a,b=Telnet(s.port),Telnet(s.port)
        try:
            for x,name in [(a,'alice'),(b,'bob')]:x.until();x.cmd('LOGIN '+name,b'> ');x.cmd(PASSWORD);x.cmd('CHAT',b'Chat> ')
            a.until(b'Chat> ');b.cmd('/LANG ja','チャット> '.encode())
            a.send(b'/KICK bob\r\n');a.until(b'connection(s) closed.');a.until(b'Chat> ')
            assert '管理者の操作'.encode() in b.until('接続を終了します。'.encode())
            a.cmd('/LEAVE');assert b'Chat removal order' in a.cmd('AUDIT')
        finally:a.close();b.close();s.close()
        print('PASS: admin-only kick, protected admins, all target chat connections closed, BBS peer unaffected, private target blocked, reconnect/preferences intact, durable filtered audit, malformed journal rejection and bilingual Telnet')

if __name__=='__main__':run(sys.argv[1])
