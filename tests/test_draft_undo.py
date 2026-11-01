import pathlib,sys,tempfile
from test_board import Server,read
from test_runtime import Client
from test_moderation import expect
from test_preview import preview
from test_telnet import Client as TelnetClient
from test_draft_edit import lines

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='undo-') as d:
        board=pathlib.Path(d)/'board'; s=Server(binary,board); a,b=Client(s.port),Client(s.port)
        try:
            expect(a,'POST original','EDIT end with . or /CANCEL'); expect(a,'/UNDO','ERR no undo')
            expect(a,'/INSERT 1 middle','DRAFT LINE INSERTED')
            expect(a,'/INSERT 1 first','DRAFT LINE INSERTED'); expect(a,'/INSERT 3 last','DRAFT LINE INSERTED')
            assert preview(a)[2]==b'first\r\nmiddle\r\nlast\r\n'
            expect(a,'/UNDO','DRAFT UNDONE'); assert preview(a)[2]==b'first\r\nmiddle\r\n'
            expect(a,'/UNDO','ERR no undo')
            for cmd in ['/TITLE modified','/REPLACE 1 日本語','/DELETE 1','/INSERT 2','/INSERT 3 tail']:
                before=preview(a); response='DRAFT TITLE CHANGED' if cmd.startswith('/TITLE') else 'DRAFT LINE REPLACED' if cmd.startswith('/REPLACE') else 'DRAFT LINE DELETED' if cmd.startswith('/DELETE') else 'DRAFT LINE INSERTED'
                expect(a,cmd,response); expect(a,'/UNDO','DRAFT UNDONE'); assert preview(a)==before
            before=preview(a); a.send(b'appended\n'); lines(a)
            expect(a,'/INSERT 0 bad','ERR draft line'); expect(a,'/INSERT 99 bad','ERR draft line')
            expect(a,'/INSERT 2 a\tb','ERR control character')
            expect(a,'/TITLE original','DRAFT TITLE CHANGED'); expect(a,'/REPLACE 1 first','DRAFT LINE REPLACED')
            expect(a,'/UNDO','DRAFT UNDONE'); assert preview(a)==before
            # Successful append is one undo step, even for escaped command text.
            a.send(b'//INSERT 1 text\n'); assert preview(a)[2].endswith(b'/INSERT 1 text\r\n'); expect(a,'/UNDO','DRAFT UNDONE')
            a.send(b'//UNDO\n'); assert preview(a)[2].endswith(b'/UNDO\r\n'); expect(a,'/UNDO','DRAFT UNDONE')
            expect(b,'POST other','EDIT end with . or /CANCEL'); expect(b,'/UNDO','ERR no undo')
            expect(a,'/CANCEL','CANCELLED'); expect(a,'POST boundary','EDIT end with . or /CANCEL')
            expect(a,'/UNDO','ERR no undo'); a.send((('x'*4094+'\n')*63+'x'*4092+'\n').encode()); lines(a)
            expect(a,'/INSERT 2','DRAFT LINE INSERTED'); assert len(preview(a)[2])==262144
            expect(a,'/INSERT 3','ERR body limit 262144 bytes; cancel or finish'); expect(a,'/UNDO','DRAFT UNDONE'); assert len(preview(a)[2])==262142
            expect(a,'/CANCEL','CANCELLED'); expect(a,'POST final','EDIT end with . or /CANCEL')
            expect(a,'/INSERT 1 saved','DRAFT LINE INSERTED'); expect(a,'.','SAVED 1')
            expect(a,'POST fresh','EDIT end with . or /CANCEL'); expect(a,'/UNDO','ERR no undo')
        finally: a.close(); b.close(); s.close()
        s=Server(binary,board,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('POST draft',b'Body > ')
            assert b'line inserted' in a.cmd('/INSERT 1 text',b'Body > ')
            assert b'edit undone' in a.cmd('/UNDO',b'Body > ')
            assert b'No edit' in a.cmd('/UNDO',b'Body > ')
            a.cmd('/CANCEL'); a.cmd('LANG ja'); a.cmd('REPLY 1 和文','本文 > '.encode())
            assert '挿入しました'.encode() in a.cmd('/INSERT 1 本文','本文 > '.encode())
            assert '戻しました'.encode() in a.cmd('/UNDO','本文 > '.encode()); a.cmd('/CANCEL')
        finally: a.close(); s.close()
        s=Server(binary,board); a=Client(s.port)
        try:
            assert read(a,1)[1:]==('final','saved\r\n'); expect(a,'READ 2','ERR no article')
            expect(a,'POST reconnect','EDIT end with . or /CANCEL'); expect(a,'/UNDO','ERR no undo')
        finally: a.close(); s.close()
        print('PASS: first/middle/end/empty insert, one-step undo of every edit type, failures/no-ops preserve history, byte limit, escaped commands, per-session/reset isolation')
        print('PASS: Japanese/English Telnet, final save and restart preserve only committed content')

if __name__=='__main__': run(sys.argv[1])
