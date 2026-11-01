"""Saved-draft reminders after authentication, without changing editor/store state."""
import pathlib,sys,tempfile,time
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient

JA='保存下書きがあります'
EN='You have a saved draft'

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='draft-notice-') as d:
        p=pathlib.Path(d); board=p/'board'; users=p/'users'; config=p/'boards'
        config.write_text('1|0|all|member|Main\n2|1|u1|u1|Private\n')
        args=dict(members=users,catalog=config)
        s=Server(binary,board,members=users); a=Client(s.port); b=Client(s.port)
        try: auth(a,'alice',True); auth(b,'bob',True)
        finally: a.close(); b.close(); s.close()
        s=Server(binary,board,**args); a=Client(s.port)
        try:
            auth(a,'alice'); a.send(b'LANG en\n'); assert b'en' in a.line(); expect(a,'PREFS SAVE','PREFS SAVED en OFF')
            expect(a,'BOARD 2','BOARD 2 Private'); expect(a,'POST private-title','EDIT end with . or /CANCEL')
            a.send(b'private-body\n'); expect(a,'/SAVE','DRAFT STORED 1'); expect(a,'/CANCEL','CANCELLED')
        finally: a.close(); s.close()
        before=(users.read_bytes(),board.read_bytes())
        # ACL loss must not suppress the owner's reminder; no title/body is disclosed.
        config.write_text('1|0|all|member|Main\n2|1|u2|u2|Private\n')
        s=Server(binary,board,**args,menu=True,language='ja'); a=Menu(s.port)
        try:
            assert EN not in a.until()
            assert EN not in a.cmd('LOGIN alice','> ')
            out=a.cmd(PASSWORD)
            assert out.count(EN)==1 and 'revision 1' in out and JA not in out
            assert 'private-title' not in out and 'private-body' not in out
            assert out.index('Logged in as alice')<out.index(EN)<out.index('] > ')
            assert EN not in a.cmd('ACCOUNT')  # reminder is not repeated on ordinary commands
            assert 'unavailable' in a.cmd('RESUME')  # login did not resume or switch boards
        finally: a.close(); s.close()
        assert before==(users.read_bytes(),board.read_bytes())
        # Raw authentication retains one response and no unsolicited reminder.
        s=Server(binary,board,**args); a=Client(s.port)
        try:
            auth(a,'alice'); expect(a,'PING','PONG'); expect(a,'DRAFT','DRAFT STORED 1')
            a.send(b'LANG ja\n'); assert b'ja' in a.line(); expect(a,'PREFS SAVE','PREFS SAVED ja OFF')
        finally: a.close(); s.close()
        before=(users.read_bytes(),board.read_bytes())
        s=Server(binary,board,**args,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('LOGIN alice',b'> '); out=a.cmd('wrong-password')
            assert EN.encode() not in out and JA.encode() not in out
            time.sleep(1.05); a.cmd('LOGIN alice',b'> '); out=a.cmd(PASSWORD)
            assert out.count(JA.encode())==1 and EN.encode() not in out and '版 1'.encode() in out
            a.cmd('LOGOUT'); time.sleep(1.05); a.cmd('LOGIN bob',b'> '); out=a.cmd(PASSWORD)
            assert JA.encode() not in out and EN.encode() not in out
        finally: a.close(); s.close()
        assert before==(users.read_bytes(),board.read_bytes())
        # Deletion tombstones retain a revision, but must never trigger a reminder.
        s=Server(binary,board,**args); a=Client(s.port)
        try: auth(a,'alice'); expect(a,'DRAFT DELETE 1','DRAFT REMOVED')
        finally: a.close(); s.close()
        s=Server(binary,board,**args,menu=True); a=Menu(s.port)
        try:
            a.until(); a.cmd('LOGIN alice','> '); out=a.cmd(PASSWORD)
            assert JA not in out and EN not in out
            assert 'ありません' in a.cmd('DRAFT')
        finally: a.close(); s.close()
        print('PASS: owner-only login reminder, restored language, Japanese/English menus and Telnet, no secret contents or automatic resume')
        print('PASS: failed login, absent/deleted draft, ACL loss, raw compatibility and byte-identical stores on login')

if __name__=='__main__': run(sys.argv[1])
