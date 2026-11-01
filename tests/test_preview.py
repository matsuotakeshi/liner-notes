import pathlib,sys,tempfile
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD,unread
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient
from test_pager import MORE,JA

def preview(c):
    c.send(b'/PREVIEW\n'); header=c.line(); assert header.startswith(b'PREVIEW PARENT ')
    title=c.line(); assert title.startswith(b'TITLE ')
    length=int(c.line().split()[1])
    while len(c.buf)<length: c.buf+=c.sock.recv(8192)
    body,c.buf=c.buf[:length],c.buf[length:]; c.expect(b'END'); return header,title,body

def screen(c,text):
    c.send(text.encode()+b'\r\n')
    while True:
        for marker in [MORE,JA,b'Body > ','本文 > '.encode(),b'] > ']:
            if marker in c.buf:
                n=c.buf.index(marker)+len(marker); out,c.buf=c.buf[:n],c.buf[n:]; return out,marker
        if isinstance(c,TelnetClient): c.receive()
        else:
            data=c.sock.recv(8192); assert data; c.buf+=data

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='preview-') as d:
        p=pathlib.Path(d); board=p/'board'; users=p/'users'; admins=p/'admins'
        s=Server(binary,board,members=users); a=Client(s.port)
        try: auth(a,'alice',True); assert post(a,'parent','body')==1
        finally: a.close(); s.close()
        admins.write_text('1\n')
        snapshot={f:f.read_bytes() for f in [board,users,p/'board.moderation']}
        s=Server(binary,board,members=users,admins=admins); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'alice'); before=unread(b)
            expect(a,'REPLY 1 題名','EDIT end with . or /CANCEL')
            h,t,body=preview(a); assert b'PARENT 1' in h and t=='TITLE 題名'.encode() and body==b''
            a.send('first 日本語\n//PREVIEW\n..dot\n'.encode())
            h,t,body=preview(a); assert body=='first 日本語\r\n/PREVIEW\r\n.dot\r\n'.encode()
            assert preview(a)[2]==body and unread(b)==before
            b.send(b'READ 2\n'); b.expect(b'ERR no article')
            expect(a,'/CANCEL','CANCELLED')
            expect(a,'POST discarded','EDIT end with . or /CANCEL'); a.send(b'not saved\n'); preview(a)
        finally: a.close(); b.close(); s.close()
        for f,raw in snapshot.items(): assert f.read_bytes()==raw
        print('PASS: empty/reply previews, exact UTF-8 body and slash/dot escaping, repeated preview/cancel/disconnect without any store changes')
        s=Server(binary,board,members=users,admins=admins); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'alice')
            expect(a,'REPLY 1 appended','EDIT end with . or /CANCEL'); a.send(b'before\n'); preview(a); a.send(b'after\n')
            assert preview(a)[2]==b'before\r\nafter\r\n'
            expect(a,'.','SAVED 2'); assert read(b,2)[2]=='before\r\nafter\r\n'
            expect(a,'REPLY 1 blocked','EDIT end with . or /CANCEL'); a.send(b'draft\n'); preview(a)
            expect(b,'HIDE 1','HIDDEN SAVED 1'); expect(a,'.','ERR article hidden')
            expect(b,'RESTORE 1','RESTORED 1'); expect(b,'READ 3','ERR no article')
        finally: a.close(); b.close(); s.close()
        print('PASS: append after preview saves once; final commit still rechecks hidden reply parent')
        s=Server(binary,board,members=users,admins=admins,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('LOGIN alice',b'> '); a.cmd(PASSWORD)
            a.cmd('PAGER ON'); a.cmd('PAGER SIZE 80 7'); a.cmd('POST visible',b'Body > ')
            for i in range(20): a.cmd(f'line-{i:02d} 本文',b'Body > ')
            out,end=screen(a,'/PREVIEW'); assert b'Unsaved preview' in out and end==MORE
            combined=out.replace(MORE,b'')
            while end==MORE:
                out,end=screen(a,''); combined+=out.replace(MORE,b'')
            assert end==b'Body > '
            for i in range(20): assert combined.count(f'line-{i:02d}'.encode())==1
            screen(a,'/PREVIEW'); out,end=screen(a,'Q'); assert end==b'Body > '
            # Q cancels only display; subsequent preview still contains the draft.
            screen(a,'/PREVIEW'); out,end=screen(a,'OFF'); assert b'line-19' in out and end==b'Body > '
            assert b'line-19' in a.cmd('/PREVIEW',b'Body > ')
            a.cmd('/CANCEL'); a.cmd('LANG ja'); a.cmd('POST 日本語', '本文 > '.encode())
            out=a.cmd('/PREVIEW','本文 > '.encode()); assert '未保存プレビュー'.encode() in out
            a.cmd('追記','本文 > '.encode()); assert '保存しました'.encode() in a.cmd('.')
        finally: a.close(); s.close()
        print('PASS: Japanese/English Telnet, pager Enter/Q/OFF, return to editor and preview cancellation preserves draft')

if __name__=='__main__': run(sys.argv[1])
