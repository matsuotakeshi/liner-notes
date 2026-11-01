import pathlib,sys,tempfile,time
from test_board import Server,post
from test_runtime import Client
from test_members import auth,mark,PASSWORD
from test_catalog import switch
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient

JA='未読記事:'
EN='Unread articles:'

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='login-unread-') as d:
        p=pathlib.Path(d);data=p/'data';users=p/'users';cfg=p/'boards';admins=p/'admins'
        s=Server(binary,data,members=users);a,b=Client(s.port),Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True)
        finally:a.close();b.close();s.close()
        cfg.write_text('1|0|all|member|Main\n2|1|u1|u1|Private\n3|2|all|member|Child\n4|1|all|u1|Read only\n');admins.write_text('1\n')
        args=dict(members=users,catalog=cfg,admins=admins)
        s=Server(binary,data,**args);a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob')
            root=post(a,'root','body');reply=post(a,'reply','body',root);hidden=post(a,'hidden','body',root)
            expect(a,f'HIDE {hidden}',f'HIDDEN SAVED {hidden}');mark(b,root)
            for board in [2,3,4]:switch(a,board);post(a,'secret-title','secret-body')
            a.send(b'LANG en\n');a.line();expect(a,'PREFS SAVE','PREFS SAVED en OFF')
            expect(b,'PREFS SAVE','PREFS SAVED ja OFF')
            expect(a,'POST draft','EDIT end with . or /CANCEL');a.send(b'draft-secret\n');expect(a,'/SAVE','DRAFT STORED 1')
        finally:a.close();b.close();s.close()
        snapshot=(data.read_bytes(),users.read_bytes(),pathlib.Path(str(data)+'.moderation').read_bytes())
        s=Server(binary,data,**args,menu=True,language='ja');a=Menu(s.port)
        try:
            assert EN not in a.until();assert EN not in a.cmd('LOGIN alice','> ')
            out=a.cmd(PASSWORD)
            assert out.count(EN)==1 and 'Unread articles: 5 / Boards with unread articles: 4.' in out
            assert JA not in out and 'secret-title' not in out and 'draft-secret' not in out
            assert out.index('Logged in as alice')<out.index(EN)<out.index('You have a saved draft')
            assert EN not in a.cmd('ACCOUNT')
        finally:a.close();s.close()
        s=Server(binary,data,**args,menu=True,telnet=True,language='en');a=TelnetClient(s.port)
        try:
            a.until();a.cmd('LOGIN bob',b'> ');out=a.cmd('wrong-password')
            assert EN.encode() not in out and JA.encode() not in out
            time.sleep(1.05);a.cmd('LOGIN bob',b'> ');out=a.cmd(PASSWORD)
            assert out.count(JA.encode())==1 and '未読記事: 2 件 / 未読のあるボード: 2。'.encode() in out
            assert EN.encode() not in out
        finally:a.close();s.close()
        assert snapshot==(data.read_bytes(),users.read_bytes(),pathlib.Path(str(data)+'.moderation').read_bytes())
        s=Server(binary,data,**args);a=Client(s.port)
        try:
            auth(a,'bob');expect(a,'PING','PONG')  # raw auth has no extra response
            mark(a,reply);switch(a,4);mark(a,6)
        finally:a.close();s.close()
        s=Server(binary,data,**args,menu=True);a=Menu(s.port)
        try:
            a.until();a.cmd('LOGIN bob','> ');assert '未読記事: 0 件 / 未読のあるボード: 0。' in a.cmd(PASSWORD)
        finally:a.close();s.close()
        cfg.write_text(cfg.read_text().replace('2|1|u1|u1|','2|1|u2|u2|'))
        s=Server(binary,data,**args,menu=True);a=Menu(s.port)
        try:
            a.until();a.cmd('LOGIN alice','> ');assert 'Unread articles: 3 / Boards with unread articles: 2.' in a.cmd(PASSWORD)
            a.cmd('SUSPEND 2')
        finally:a.close();s.close()
        s=Server(binary,data,**args,menu=True);a=Menu(s.port)
        try:
            a.until();a.cmd('LOGIN bob','> ');out=a.cmd(PASSWORD);assert JA not in out and EN not in out
        finally:a.close();s.close()
        print('PASS: per-member visible unread totals, reply/hidden handling, ancestor ACL and read-only boards, zero counts and restart ACL changes')
        print('PASS: saved language, Japanese/English and Telnet, draft notice coexistence, failed/suspended login privacy, raw compatibility and unchanged stores')

if __name__=='__main__':run(sys.argv[1])
