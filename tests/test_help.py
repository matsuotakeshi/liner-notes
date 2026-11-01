from preference_assertions import assert_preferences_only
import pathlib,sys,tempfile
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_ui import Menu
from test_telnet import Client as TelnetClient
from test_list_pager import command,screens,MORE
from test_preview import preview
from test_moderation import expect

TOPICS=['READ','UNREAD','POST','DRAFT','SETTINGS','ACCOUNT','ADMIN']
def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='help-') as d:
        p=pathlib.Path(d);data=p/'data';users=p/'users'
        s=Server(binary,data,members=users);a=Client(s.port)
        try:
            auth(a,'alice',True)
            for i in range(7):post(a,f'topic-{i}','body')
        finally:a.close();s.close()
        snapshot=(data.read_bytes(),users.read_bytes())
        s=Server(binary,data,members=users,menu=True,language='en');a=Menu(s.port);b=Menu(s.port)
        try:
            a.until();b.until();assert 'Topic help' in b.cmd('H')
            for topic in TOPICS:assert 'Unknown help topic' not in b.cmd('HELP '+topic)
            assert 'Unknown help topic' in b.cmd('HELP wrong')
            a.cmd('LOGIN alice','> ');a.cmd(PASSWORD);a.cmd('NOTES')
            assert 'RTBBS-style boards and reading' in a.cmd('HELP read')
            assert 'topic-6' in a.cmd('+')
            a.cmd('READ 1');a.cmd('HELP UNREAD');assert 'topic-0' in a.cmd('TREE')
            a.cmd('PAGER SIZE 80 7');a.cmd('PAGER ON');out,n=screens(a,'HELP POST');assert n>1 and b'/UNDO' in out
            out,end=command(a,'HELP');assert end==MORE
            out,end=command(a,'Q');assert b'Help closed' in out and end==b'] > '
            assert 'PONG' in b.cmd('PING')
            command(a,'HELP POST');out,end=command(a,'OFF');assert b'/UNDO' in out and end==b'] > '
            a.cmd('LANG ja');assert '操作別ヘルプ' in a.cmd('HELP')
            for topic in TOPICS:assert '見つかりません' not in a.cmd('HELP '+topic)
            assert 'Liner notes' in a.cmd('MENU')
        finally:a.close();b.close();s.close()
        # Editor help must preserve body/undo and return to the editor after paging/cancel.
        s=Server(binary,data,members=users,menu=True,telnet=True,language='en');a=TelnetClient(s.port)
        try:
            a.until();a.cmd('LOGIN alice',b'> ');a.cmd(PASSWORD);a.cmd('LANG en');a.cmd('PAGER OFF')
            assert b'Saving and resuming' in a.cmd('HELP DRAFT')
            a.cmd('PAGER SIZE 80 7');a.cmd('PAGER ON');a.cmd('POST help',b'Body > ')
            a.cmd('keep',b'Body > ');out,end=command(a,'/HELP');assert end==MORE
            out=a.cmd('Q',b'Body > ');assert b'Help closed' in out
            a.cmd('/UNDO',b'Body > ');a.cmd('//HELP',b'Body > ')
            a.cmd('/CANCEL');a.cmd('PAGER OFF')
        finally:a.close();s.close()
        s=Server(binary,data,members=users);a=Client(s.port)
        try:
            for topic in ['']+TOPICS+['unknown','x'*40]:
                a.send(('HELP'+(' '+topic if topic else '')+'\n').encode())
                while a.line()!=b'END HELP':pass
            auth(a,'alice');expect(a,'POST draft','EDIT end with . or /CANCEL');a.send(b'keep\n')
            a.send(b'/HELP\n')
            while a.line()!=b'END HELP':pass
            assert preview(a)[2]==b'keep\r\n'
            expect(a,'/UNDO','DRAFT UNDONE');a.send(b'//HELP\n');assert preview(a)[2]==b'/HELP\r\n'
            expect(a,'/CANCEL','CANCELLED');expect(a,'PING','PONG')
        finally:a.close();s.close()
        assert snapshot[0]==data.read_bytes()
        assert_preferences_only(snapshot[1],users.read_bytes())
        print('PASS: all help topics in Japanese/English, guest access, unknown topics, unchanged list cursor, screen paging/cancel/OFF and session isolation')
        print('PASS: Telnet editor help, preserved body/Undo, literal /HELP, raw END HELP framing and unchanged stores')

if __name__=='__main__':run(sys.argv[1])
