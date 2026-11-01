import pathlib,sys,tempfile
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_moderation import expect
from test_preview import preview,screen
from test_telnet import Client as TelnetClient
from test_pager import MORE,JA

def lines(c):
    c.send(b'/LINES\n'); h=c.line(); assert h.startswith(b'DRAFT LINES ')
    title=c.line(); rows=[]
    while True:
        row=c.line()
        if row==b'END DRAFT': break
        rows.append(row)
    assert len(rows)==int(h.split()[2]); return title,rows

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='draft-edit-') as d:
        p=pathlib.Path(d); board=p/'board'; users=p/'users'
        s=Server(binary,board,members=users); a=Client(s.port)
        try: auth(a,'alice',True); post(a,'root','body')
        finally: a.close(); s.close()
        snapshot={f:f.read_bytes() for f in [board,users,p/'board.moderation']}
        s=Server(binary,board,members=users); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'alice')
            expect(a,'REPLY 1 original','EDIT end with . or /CANCEL')
            assert lines(a)==(b'TITLE original',[])
            expect(a,'/DELETE 1','ERR draft line')
            a.send('first\n\nlast 日本語\n'.encode())
            expect(a,'/TITLE 新しい題名','DRAFT TITLE CHANGED')
            expect(a,'/REPLACE 2 middle','DRAFT LINE REPLACED')
            expect(a,'/REPLACE 1 .','DRAFT LINE REPLACED')
            assert preview(a)[2]=='.\r\nmiddle\r\nlast 日本語\r\n'.encode()
            expect(a,'/DELETE 2','DRAFT LINE DELETED')
            assert lines(a)[1]==[b'LINE 1 .','LINE 2 last 日本語'.encode()]
            expect(a,'/REPLACE 2','DRAFT LINE REPLACED'); assert preview(a)[2]==b'.\r\n\r\n'
            baseline=preview(a)
            for cmd,error in [('/REPLACE 0 x','ERR draft line'),('/DELETE -1','ERR draft line'),('/DELETE 999999999999','ERR draft line'),('/DELETE 1 extra','ERR draft line'),('/REPLACE 3 x','ERR draft line'),('/TITLE','ERR title 1..80 bytes, no controls'),('/TITLE '+'x'*81,'ERR title 1..80 bytes, no controls'),('/REPLACE 1 a\tb','ERR control character')]:
                expect(a,cmd,error); assert preview(a)==baseline
            expect(a,'/DELETE 2','DRAFT LINE DELETED'); expect(a,'/DELETE 1','DRAFT LINE DELETED')
            expect(a,'.','ERR empty body')
            a.send(b'//TITLE literal\n//REPLACE 2 literal\n//DELETE 1\n//LINES\n//PREVIEW\n..dot\n')
            assert preview(a)[2]==b'/TITLE literal\r\n/REPLACE 2 literal\r\n/DELETE 1\r\n/LINES\r\n/PREVIEW\r\n.dot\r\n'
            expect(b,'READ 2','ERR no article'); expect(a,'/CANCEL','CANCELLED')
            expect(a,'POST bounds','EDIT end with . or /CANCEL'); a.send((('x'*4094+'\n')*63+'x'*4090+'\n\n\n').encode())
            assert len(preview(a)[2])==262144
            expect(a,'/REPLACE 65 a','ERR body limit 262144 bytes; cancel or finish'); assert len(preview(a)[2])==262144
            expect(a,'/DELETE 66','DRAFT LINE DELETED'); expect(a,'/REPLACE 65 yz','DRAFT LINE REPLACED'); assert len(preview(a)[2])==262144
            expect(a,'/CANCEL','CANCELLED')
            expect(a,'POST empty-lines','EDIT end with . or /CANCEL'); a.send(b'\n'*512)
            assert len(lines(a)[1])==512
        finally: a.close(); b.close(); s.close()
        for f,raw in snapshot.items(): assert f.read_bytes()==raw
        print('PASS: title and line edits, renumbering, empty replacement, invalid-operation atomicity, escapes, 262144-byte boundary and 512-line display; no persistence before save')
        s=Server(binary,board,members=users,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('LOGIN alice',b'> '); a.cmd(PASSWORD); a.cmd('PAGER ON'); a.cmd('PAGER SIZE 80 7')
            a.cmd('REPLY 1 draft',b'Body > ')
            for i in range(12): a.cmd(f'row-{i:02d}',b'Body > ')
            out,end=screen(a,'/LINES'); assert end==MORE and b'Unsaved draft' in out
            while end==MORE: out,end=screen(a,'')
            assert end==b'Body > '
            assert b'title changed' in a.cmd('/TITLE final',b'Body > ')
            assert b'line replaced' in a.cmd('/REPLACE 1 changed',b'Body > ')
            assert b'line deleted' in a.cmd('/DELETE 12',b'Body > ')
            screen(a,'/LINES'); out,end=screen(a,'Q'); assert end==b'Body > '
            assert b'Saved article #2' in a.cmd('.')
            a.cmd('PAGER OFF'); a.cmd('LANG ja'); a.cmd('POST 和文','本文 > '.encode())
            assert '題名を変更'.encode() in a.cmd('/TITLE 修正','本文 > '.encode())
            a.cmd('本文','本文 > '.encode()); assert '行を修正'.encode() in a.cmd('/REPLACE 1 修正文','本文 > '.encode())
            assert '未保存の下書き'.encode() in a.cmd('/LINES','本文 > '.encode()); a.cmd('/CANCEL')
        finally: a.close(); s.close()
        s=Server(binary,board,members=users); a=Client(s.port)
        try:
            parent,title,body=read(a,2); assert parent==1 and title=='final'
            assert body=='changed\r\n'+''.join(f'row-{i:02d}\r\n' for i in range(1,11))
        finally: a.close(); s.close()
        print('PASS: Japanese/English Telnet, numbered-list paging/cancel, save after editing, correct reply parent and restart persistence')

if __name__=='__main__': run(sys.argv[1])
