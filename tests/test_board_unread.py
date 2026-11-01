import pathlib,sys,tempfile,re
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,mark
from test_catalog import boards,switch
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient

def counts(c):
    result={}
    for row in boards(c):
        m=re.match(rb"\s*#(\d+) parent=\d+ depth=\d+ write=(?:yes|no) unread=(\d+|-) ",row)
        assert m,row
        result[int(m[1])]=None if m[2]==b'-' else int(m[2])
    return result

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='board-unread-') as d:
        p=pathlib.Path(d); data=p/'data'; users=p/'users'; config=p/'boards'; admins=p/'admins'
        s=Server(binary,data,members=users); a,b=Client(s.port),Client(s.port)
        try: auth(a,'alice',True); auth(b,'bob',True)
        finally: a.close(); b.close(); s.close()
        config.write_text('1|0|all|member|Main\n2|1|u1|u1|Private\n3|2|all|member|Private child\n'+''.join(f'{i}|1|all|member|Board-{i}\n' for i in range(4,12)))
        admins.write_text('1\n'); args=dict(members=users,catalog=config,admins=admins)
        s=Server(binary,data,**args); a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'bob')
            assert set(counts(a).values())=={0}
            parent=post(a,'parent','body'); reply=post(a,'reply','body',parent)
            assert counts(a)[1]==2 and counts(b)[1]==2
            read(b,parent); assert counts(b)[1]==2  # reading alone is not acknowledgement
            mark(b,parent); assert counts(b)[1]==1 and counts(a)[1]==2
            mark(b,parent,False); assert counts(b)[1]==2
            expect(a,f'HIDE {reply}',f'HIDDEN SAVED {reply}')
            assert counts(a)[1]==1 and counts(b)[1]==1
            expect(a,f'RESTORE {reply}',f'RESTORED {reply}'); assert counts(b)[1]==2
            switch(a,2); post(a,'private','secret'); switch(a,3); post(a,'child','secret')
            switch(a,11); post(a,'last','body')
            ca=counts(a); assert ca[1]==2 and ca[2]==1 and ca[3]==1 and ca[11]==1
            assert 2 not in counts(b) and 3 not in counts(b)
            cg=counts(g); assert 2 not in cg and 3 not in cg and set(cg.values())=={None}
            assert len(ca)==11 and len(counts(b))==9  # crosses raw result page boundary
            switch(b,1); mark(b,parent); mark(b,reply); assert counts(b)[1]==0
        finally: a.close(); b.close(); g.close(); s.close()
        snapshot=(data.read_bytes(),users.read_bytes(),pathlib.Path(str(data)+'.moderation').read_bytes())
        # Restart preserves per-member counts; plain menu and Telnet use localized labels.
        s=Server(binary,data,**args,menu=True); a=Menu(s.port)
        try:
            a.until(); a.cmd('LOGIN bob','> '); from test_members import PASSWORD
            a.cmd(PASSWORD); out=a.cmd('BOARDS')
            assert '未読: 0' in out and 'unread=' not in out and 'Private' not in out
            out=a.cmd('+'); assert 'Board-11' in out and '未読: 1' in out
        finally: a.close(); s.close()
        s=Server(binary,data,**args,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); assert b'Unread: -' in a.cmd('BOARDS')
            a.cmd('LOGIN alice',b'> '); a.cmd(PASSWORD)
            out=a.cmd('BOARDS'); assert b'Unread: 2' in out and b'Unread: 1' in out and b'unread=' not in out
        finally: a.close(); s.close()
        assert snapshot==(data.read_bytes(),users.read_bytes(),pathlib.Path(str(data)+'.moderation').read_bytes())
        print('PASS: per-member counts, replies, explicit SEEN/UNSEEN, hidden/restore, direct-board totals, ACL inheritance, guests and multiple result pages')
        print('PASS: restart, Japanese/English menus and Telnet, localized counts and byte-identical article/member/moderation stores')

if __name__=='__main__': run(sys.argv[1])
