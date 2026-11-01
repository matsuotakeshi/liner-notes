"""Mail draft reminders respect authentication, owner, locale and persistence."""
import pathlib,sys,tempfile,time
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_talk import command
from test_ui import Menu
from test_telnet import Client as Telnet
JA='保存中のメール下書きがあります'
EN='You have a saved mail draft'

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='mailnotice-') as d:
        p=pathlib.Path(d);data=p/'bbs';members=p/'members'
        s=Server(binary,data,members=members);a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice',True);auth(b,'bob',True)
            command(a,'POST article-secret',b'EDIT ');command(a,'/SAVE',b'DRAFT STORED');command(a,'/CANCEL',b'CANCELLED')
            command(a,'MAIL SEND bob mail-secret',b'MAIL EDIT ');a.send(b'private-body\n');command(a,'/SAVE',b'MAIL DRAFT SAVED')
            command(a,'0',b'MAIL CLOSED');a.send(b'LANG en\n');assert b'en' in a.line();command(a,'PREFS SAVE',b'PREFS SAVED')
        finally:a.close();b.close();s.close()
        snapshot=lambda:tuple(f.read_bytes() for f in [data,members,p/'members.mail'])
        before=snapshot()
        s=Server(binary,data,members=members,menu=True,language='ja');a=Menu(s.port)
        try:
            assert EN not in a.until();assert EN not in a.cmd('LOGIN alice','> ')
            out=a.cmd(PASSWORD)
            assert out.count(EN)==1 and JA not in out and 'MAIL RESUME' in out and 'MAIL DRAFT' in out
            assert 'You have a saved draft' in out and 'Unread mail:' in out
            assert 'mail-secret' not in out and 'private-body' not in out and 'article-secret' not in out
            assert EN not in a.cmd('ACCOUNT')
        finally:a.close();s.close()
        assert snapshot()==before
        s=Server(binary,data,members=members);a=Client(s.port)
        try:
            auth(a,'alice');command(a,'PING',b'PONG') # raw response stays unchanged
            a.send(b'LANG ja\n');assert b'ja' in a.line();command(a,'PREFS SAVE',b'PREFS SAVED')
        finally:a.close();s.close()
        before=snapshot()
        s=Server(binary,data,members=members,menu=True,telnet=True,language='en');a=Telnet(s.port)
        try:
            a.until();a.cmd('LOGIN alice',b'> ');out=a.cmd('wrong-password');assert JA.encode() not in out and EN.encode() not in out
            time.sleep(1.05);a.cmd('LOGIN alice',b'> ');out=a.cmd(PASSWORD);assert out.count(JA.encode())==1 and EN.encode() not in out
            a.cmd('LOGOUT');time.sleep(1.05);a.cmd('LOGIN bob',b'> ');out=a.cmd(PASSWORD);assert JA.encode() not in out and EN.encode() not in out
        finally:a.close();s.close()
        assert snapshot()==before
        # Sent and explicitly deleted drafts must not leave a reminder.
        for action in ['send','delete']:
            s=Server(binary,data,members=members);a=Client(s.port)
            try:
                auth(a,'alice')
                if action=='send':command(a,'MAIL RESUME',b'MAIL DRAFT ');command(a,'.',b'MAIL SENT')
                else:
                    command(a,'MAIL SEND bob second',b'MAIL EDIT ');command(a,'/SAVE',b'MAIL DRAFT SAVED')
                    out=command(a,'MAIL DRAFT',b'MAIL DRAFT ');revision=int(out.split()[2]);command(a,'MAIL DRAFT DELETE '+str(revision),b'MAIL DRAFT DELETED')
            finally:a.close();s.close()
            s=Server(binary,data,members=members,menu=True);a=Menu(s.port)
            try:
                a.until();a.cmd('LOGIN alice','> ');out=a.cmd(PASSWORD);assert JA not in out and EN not in out
            finally:a.close();s.close()
    print('PASS: owner-only mail draft login notice, saved language, Japanese/English menu and Telnet, coexisting article/unread notices, failed-login and raw isolation, unchanged stores, no notice after send/delete')
if __name__=='__main__':run(sys.argv[1])
