"""History is authorized, bounded, read-only, and safe before further appends."""
import os
import pathlib
import struct
import sys
import tempfile
import time
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_moderation import expect,checksum
from test_notes import page
from test_ui import Menu
from test_telnet import Client as TelnetClient


def history(c,command='AUDIT'):
    header,rows,next_id=page(c,command)
    assert header.startswith(b'AUDIT ')
    entries=[]
    for row in rows:
        parts=row.split(); assert parts[0]==b'EVENT' and len(parts)==8,row
        entries.append((int(parts[1]),parts[2].decode(),int(parts[3]),parts[4].decode(),parts[5].decode(),int(parts[6]),int(parts[7])))
    return entries,next_id


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='audit-') as d:
        p=pathlib.Path(d); data=p/'board'; users=p/'users'; journal=p/'board.moderation'; admins=p/'admins'; config=p/'boards'
        s=Server(binary,data,members=users); a,c,b=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice',True); auth(c,'carol',True); auth(b,'bob',True)
            assert post(b,'Hidden secret title','Hidden secret body')==1
        finally: a.close(); b.close(); c.close(); s.close()
        admins.write_text('1\n2\n'); config.write_text('1|0|all|member|Public\n2|0|u2|u2|Private\n')
        args=dict(members=users,catalog=config,admins=admins)
        s=Server(binary,data,**args); a,c,b=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(c,'carol'); auth(b,'bob')
            assert history(a)==([],0)
            expect(c,'BOARD 2','BOARD 2 Private'); assert post(c,'private title','private body')==2
            for _ in range(6):
                expect(a,'HIDE 1','HIDDEN SAVED 1'); expect(a,'RESTORE 1','RESTORED 1')
            expect(a,'SUSPEND 3','SUSPENDED 3'); expect(a,'RESUME 3','RESUMED 3')
            expect(c,'HIDE 2','HIDDEN SAVED 2'); expect(c,'RESTORE 2','RESTORED 2')
        finally: a.close(); b.close(); c.close(); s.close()
        original=journal.read_bytes(); source=data.read_bytes(); account=users.read_bytes()
        assert len(original)==8+16*32
        s=Server(binary,data,**args); a,c,b,g=Client(s.port),Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(c,'carol'); auth(b,'bob')
            expect(b,'AUDIT','ERR admin required'); expect(g,'AUDIT 1','ERR admin required')
            first,cursor=history(a); assert [e[0] for e in first]==list(range(14,6,-1)) and cursor==6
            tail,last=history(a,f'AUDIT {cursor}'); assert [e[0] for e in tail]==list(range(6,0,-1)) and last==0
            assert {e[4] for e in first+tail}=={'HIDE','RESTORE','SUSPEND','RESUME'}
            assert all(e[2:4]==(1,'alice') and e[1].endswith('Z') for e in first+tail)
            full,_=history(c); assert [e[0] for e in full]==list(range(16,8,-1))
            assert full[0][2:]==(2,'carol','RESTORE',2,2)
            assert history(a,'AUDIT 1')[0][0][4]=='HIDE'
            for cmd in ['AUDIT 0','AUDIT -1','AUDIT +1','AUDIT 17','AUDIT 4294967295','AUDIT 4294967296','AUDIT 999999999999999999999','AUDIT 1 extra']:
                expect(a,cmd,'ERR audit cursor')
            assert history(a)[0]==first
            print('PASS: admin-only history, per-board read ACLs, four actions, actor ID/name, UTC and newest-first pagination')
        finally: a.close(); b.close(); c.close(); g.close(); s.close()
        assert journal.read_bytes()==original and data.read_bytes()==source and users.read_bytes()==account
        print('PASS: history inspection, denied requests and invalid cursors never rewrite data or add audit records')
        s=Server(binary,data,**args); a=Client(s.port)
        try:
            auth(a,'alice'); _,cursor=history(a)
            history(a,'AUDIT 3')  # Leave the journal's read offset away from EOF.
            expect(a,'HIDE 1','HIDDEN SAVED 1')
            assert [e[0] for e in history(a,f'AUDIT {cursor}')[0]]==list(range(6,0,-1))
            assert history(a)[0][0][0]==17
            expect(a,'SUSPEND 3','SUSPENDED 3'); history(a,'AUDIT 2')
            expect(a,'RESUME 3','RESUMED 3'); expect(a,'RESTORE 1','RESTORED 1')
        finally: a.close(); s.close()
        complete=journal.read_bytes(); assert complete.startswith(original) and len(complete)==8+20*32
        s=Server(binary,data,**args,menu=True); a=Menu(s.port)
        try:
            a.until(); a.cmd('LOGIN alice','> '); a.cmd(PASSWORD)
            view=a.cmd('AUDIT'); assert '管理操作履歴' in view and '操作者 1:alice' in view and '会員の停止解除' in view
            assert 'Hidden secret' not in view and 'private' not in view
            assert '#1 ' in a.cmd('1')  # Numeric selection means history sequence, not READ.
            a.cmd('LANG en'); assert 'Moderation history' in a.cmd('AUDIT')
            assert 'Actor' in a.cmd('+')
        finally: a.close(); s.close()
        s=Server(binary,data,**args,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('LOGIN alice',b'> '); a.cmd(PASSWORD)
            assert b'Moderation history' in a.cmd('AUDIT') and b'Actor' in a.cmd('+')
        finally: a.close(); s.close()
        assert journal.read_bytes()==complete
        print('PASS: reads preserve later article/account appends; paging remains stable across new events and restart')
        print('PASS: localized Japanese/English history, numeric history navigation and Telnet')
        # Large history: bounded scans even when all recent records are private.
        fixture=bytearray(b'RTMOD001')
        for seq in range(1,10021):
            row=bytearray(32); row[0]=ord('M'); row[1]=seq%2
            struct.pack_into('<IIIIQ',row,4,seq,1 if seq==1 else 2,1 if seq==1 else 2,2,1700000000+seq)
            row[28:]=checksum(row); fixture+=row
        journal.write_bytes(fixture)
        s=Server(binary,data,**args,menu=True); a,c=Menu(s.port),Menu(s.port)
        try:
            for client,name in [(a,'alice'),(c,'carol')]:
                client.until(); client.cmd('LOGIN '+name,'> '); client.cmd(PASSWORD); client.cmd('RAW','RAW\r\n')
            rows,cursor=history(a); assert rows==[] and cursor==10020-256
            start=time.monotonic(); expect(c,'PING','PONG'); assert time.monotonic()-start<1
            found=[]; pages=1
            while cursor:
                rows,next_id=history(a,f'AUDIT {cursor}'); found+=rows; pages+=1
                assert next_id<cursor; cursor=next_id
            assert [r[0] for r in found]==[1] and pages==40
            assert history(c,'AUDIT 10001')[0][0][0]==10001
            c.cmd('MENU'); c.cmd('AUDIT'); assert '#10001 ' in c.cmd('10001')
        finally: a.close(); c.close(); s.close()
        assert journal.read_bytes()==fixture
        print('PASS: history above 10000 entries, uint32 cursors, bounded 256-record scans and isolated connections')
        if os.name=='posix':
            s=Server(binary,data,**args); a=Client(s.port)
            try:
                auth(a,'alice')
                with journal.open('r+b') as f: f.truncate(8)
                expect(a,'AUDIT','ERR moderation storage unavailable')
                expect(a,'HIDE 1','ERR moderation storage unavailable')
                expect(a,'READ 1','ERR moderation storage unavailable')
                expect(a,'PING','PONG')
            finally: a.close(); s.close()
            assert journal.read_bytes()==b'RTMOD001'
            print('PASS: runtime history read failure fails closed without further writes; other I/O survives')


if __name__=='__main__': run(sys.argv[1])
