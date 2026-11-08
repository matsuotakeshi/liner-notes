"""Authority, all read paths, durable moderation, and real short-write failure."""
import os
import pathlib
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
from test_runtime import Client
from test_board import Server,post,read
from test_members import auth,unread,PASSWORD
from test_notes import page,navigate
from test_ui import Menu
from test_telnet import Client as TelnetClient


def expect(c,command,result):
    c.send((command+'\n').encode()); c.expect(result.encode())


def inspect(c,id):
    c.send(f'INSPECT {id}\n'.encode())
    header=c.line(); title=c.line(); size=int(c.line().split()[1])
    while len(c.buf)<size: c.buf+=c.sock.recv(8192)
    body,c.buf=c.buf[:size],c.buf[size:]; c.expect(b'END')
    return header,title,body


def checksum(row):
    h=2166136261
    for b in row[:28]: h=((h^b)*16777619)&0xffffffff
    return struct.pack('<I',h)


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='moderation-') as d:
        p=pathlib.Path(d); data=p/'board'; users=p/'users'; journal=p/'board.moderation'; admins=p/'admins'; catalog=p/'catalog'
        s=Server(binary,data,members=users); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice',True); auth(b,'bob',True)
            expect(a,'ADMIN','ROLE member')  # First registered user has no special rights.
            for command in ['HIDE 1','RESTORE 1','INSPECT 1']:
                expect(a,command,'ERR admin required')
            assert post(b,'secret-root','secret-root-body')==1
            assert post(b,'secret-child','secret-child-body',1)==2
            assert post(b,'sibling','sibling body',1)==3
            assert post(b,'grandchild','grandchild body',2)==4
            assert post(b,'other','other body')==5
        finally: a.close(); b.close(); s.close()
        admins.write_text('# Explicit operator-assigned member IDs\n1\n',encoding='utf-8')
        catalog.write_text('1|0|all|member|Public\n2|0|u2|u2|Private\n3|0|all|u2|Notices\n',encoding='utf-8')
        s=Server(binary,data,members=users,catalog=catalog,admins=admins)
        a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'bob'); expect(a,'ADMIN','ROLE admin'); expect(b,'ADMIN','ROLE member'); expect(g,'ADMIN','ROLE guest')
            expect(b,'BOARD 2','BOARD 2 Private'); private=post(b,'private-secret','private body')
            expect(a,'BOARD 2','ERR board unavailable')
            expect(a,f'HIDE {private}','ERR no article')
            expect(b,'BOARD 3','BOARD 3 Notices'); notice=post(b,'notice','notice body')
            expect(a,'BOARD 3','BOARD 3 Notices'); expect(a,'POST nope','ERR posting denied')
            expect(a,f'HIDE {notice}',f'HIDDEN SAVED {notice}'); expect(a,f'RESTORE {notice}',f'RESTORED {notice}')
            expect(a,'BOARD 1','BOARD 1 Public'); expect(b,'BOARD 1','BOARD 1 Public')
            a.close(); b.close(); g.close(); s.close()
            before=data.read_bytes()
            s=Server(binary,data,members=users,catalog=catalog,admins=admins)
            a,b,g=Client(s.port),Client(s.port),Client(s.port)
            auth(a,'alice'); auth(b,'bob')
            # A draft accepted before moderation must be checked again at commit.
            expect(b,'REPLY 2 started before hide','EDIT end with . or /CANCEL')
            expect(a,'HIDE 2','HIDDEN SAVED 2')
            b.send(b'draft\n.\n'); b.expect(b'ERR article hidden')
            for c in [a,g]: expect(c,'READ 2','HIDDEN 2 PARENT 1')
            for c in [b,g]:
                for command in ['HIDE 2','RESTORE 2','INSPECT 2']: expect(c,command,'ERR admin required')
                for command in ['REPLY 2 forbidden']: expect(c,command,'ERR article hidden' if c==b else 'ERR login required')
            header,title,body=inspect(a,2); assert b'HIDDEN 1' in header and title==b'TITLE secret-child' and body==b'secret-child-body\r\n'
            expect(a,'HIDE 1','HIDDEN SAVED 1')
            length=journal.stat().st_size; expect(a,'HIDE 1','HIDDEN SAVED 1'); assert journal.stat().st_size==length
            for c in [a,b,g]:
                for command in ['LIST','NOTES','TREE 1','TREE 2']:
                    h,rows,_=page(c,command); output=b'\n'.join([h]+rows)
                    assert b'secret-root' not in output and b'secret-child' not in output
                    assert not any(row.startswith((b'HIDDEN ',b'WITHDRAWN ')) for row in rows)
                    assert b'sibling' not in output and b'grandchild' not in output
                expect(c,'READ 1','HIDDEN 1 PARENT 0'); expect(c,'CHILD','ERR no neighbour')
                expect(c,'READ 4','HIDDEN 4 PARENT 2'); expect(c,'PARENT','ERR no neighbour')
            assert unread(b)==[5]
            assert unread(b,1)==[]
            navigate(b,'NEW',5)
            expect(b,'SEEN 1','ERR article hidden'); expect(b,'UNSEEN 2','ERR article hidden')
            expect(a,'INSPECT 999','ERR no article')
            print('PASS: explicit admin bootstrap, worker-side authority, board ACLs and read-only-board moderation')
            print('PASS: hidden content excluded from READ/LIST/NOTES/TREE/navigation/unread; children and IDs preserved')
            print('PASS: draft commit rechecks hidden parent; admin-only inspection and idempotent hide')
        finally: s.close(crash=True); a.close(); b.close(); g.close()
        assert data.read_bytes()==before
        s=Server(binary,data,members=users,catalog=catalog,admins=admins); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'bob'); expect(b,'READ 1','HIDDEN 1 PARENT 0'); assert unread(b)==[5]
            expect(a,'RESTORE 1','RESTORED 1')
            assert unread(b)==[1,3,5]
            expect(b,'READ 4','HIDDEN 4 PARENT 2')
            expect(a,'RESTORE 2','RESTORED 2')
            assert read(b,1)[1:] == ('secret-root','secret-root-body\r\n')
            assert read(b,2)[0]==1 and read(b,4)[0]==2
            assert unread(b)==[1,2,3,4,5]  # Hide did not silently mark anything read.
            expect(a,'HIDE 2','HIDDEN SAVED 2')
        finally: a.close(); b.close(); s.close()
        assert data.read_bytes()==before
        raw=journal.read_bytes(); assert raw[:8]==b'RTMOD001' and (len(raw)-8)%32==0
        for seq,offset in enumerate(range(8,len(raw),32),1):
            row=raw[offset:offset+32]
            assert row[0]==ord('M') and struct.unpack_from('<I',row,4)[0]==seq
            assert struct.unpack_from('<I',row,16)[0]==1 and struct.unpack_from('<Q',row,20)[0]>0
            assert row[28:]==checksum(row)
        print('PASS: forced restart retains hides; restore preserves exact source and unread state; durable actor/time audit')
        # Removing admin configuration does not remove moderation or grant a bypass.
        s=Server(binary,data,members=users,catalog=catalog); a=Client(s.port)
        try:
            auth(a,'alice'); expect(a,'ADMIN','ROLE member'); expect(a,'RESTORE 2','ERR admin required'); expect(a,'READ 2','HIDDEN 2 PARENT 1')
        finally: a.close(); s.close()
        for invalid in ['999\n','0\n','1\n1\n','alice\n','1000000000000000000000000000000\n']:
            admins.write_text(invalid)
            result=subprocess.run([binary,'--admins',str(admins),'0',str(data),str(users),str(catalog)],capture_output=True,timeout=10)
            assert result.returncode and b'admins:' in result.stderr
            assert journal.read_bytes()==raw and data.read_bytes()==before
        admins.write_text('1\n')
        badrows=[raw[:-1],raw[:-1]+bytes([raw[-1]^1])]
        for pos,value in [(8,9999),(12,99),(16,999),(4,999)]:
            broken=bytearray(raw); row=bytearray(broken[-32:]); struct.pack_into('<I',row,pos,value); row[28:]=checksum(row); broken[-32:]=row; badrows.append(bytes(broken))
        for broken in badrows:
            journal.write_bytes(broken)
            result=subprocess.run([binary,'--admins',str(admins),'0',str(data),str(users),str(catalog)],capture_output=True,timeout=10)
            assert result.returncode and b'moderation:' in result.stderr
            assert journal.read_bytes()==broken and data.read_bytes()==before
        journal.write_bytes(raw)
        print('PASS: revoked roles, invalid admin config, damaged/truncated/mismatched journals fail without rewriting originals')
        # Same commands and presentation through menu and Telnet adapters.
        s=Server(binary,data,members=users,catalog=catalog,admins=admins,menu=True)
        a=Menu(s.port)
        try:
            a.until(); a.cmd('LOGIN alice','> '); a.cmd(PASSWORD)
            assert '管理者' in a.cmd('ADMIN')
            assert '非表示' in a.cmd('READ 2')
            assert '管理者確認' in a.cmd('INSPECT 2')
            a.cmd('LANG en'); assert 'Administrator' in a.cmd('ADMIN')
            assert 'now visible' in a.cmd('RESTORE 2')
            assert 'now hidden' in a.cmd('HIDE 2')
        finally: a.close(); s.close()
        s=Server(binary,data,members=users,catalog=catalog,admins=admins,menu=True,telnet=True,language='en')
        a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('LOGIN alice',b'> '); a.cmd(PASSWORD)
            assert b'Administrator' in a.cmd('ADMIN')
            assert b'now visible' in a.cmd('RESTORE 2')
            assert b'now hidden' in a.cmd('HIDE 2')
        finally: a.close(); s.close()
        print('PASS: Japanese/English menus and real Telnet share moderation authority and behavior')
        if os.name=='posix':
            import fault_io as resource
            limit=journal.stat().st_size+16
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,data,members=users,catalog=catalog,admins=admins,preexec_fn=size_limit,options=["--user-log","-"])
            a,b=Client(s.port),Client(s.port)
            try:
                auth(a,'alice'); auth(b,'bob')
                expect(a,'RESTORE 2','ERR moderation storage uncertain')
                expect(a,'READ 2','ERR moderation storage unavailable')
                expect(b,'LIST','ERR moderation storage unavailable')
                expect(b,'PING','PONG')
            finally: a.close(); b.close(); s.close()
            assert journal.stat().st_size==limit and data.read_bytes()==before
            result=subprocess.run([binary,'0',str(data),str(users),str(catalog)],capture_output=True,timeout=10)
            assert result.returncode and b'moderation:' in result.stderr
            print('PASS: OS short-write never acknowledges restore; board access stops, corrupt restart refused, other I/O survives')


if __name__=='__main__': run(sys.argv[1])
