import pathlib,sys,tempfile,re
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,mark,PASSWORD
from test_notes import page
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient

def counts(c):
    result={};cursor=1
    while cursor:
        _,rows,cursor=page(c,f'NOTES {cursor}')
        for row in rows:
            if row.startswith(b'HIDDEN '):continue
            m=re.match(rb'#(\d+) replies=\d+ latest=\d+ updated=\S+ unread=(\d+|-) ',row)
            assert m,row
            result[int(m[1])]=None if m[2]==b'-' else int(m[2])
    return result

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='thread-unread-') as d:
        p=pathlib.Path(d);data=p/'data';users=p/'users';admins=p/'admins'
        s=Server(binary,data,members=users);a,b=Client(s.port),Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True)
        finally:a.close();b.close();s.close()
        admins.write_text('1\n');args=dict(members=users,admins=admins)
        s=Server(binary,data,**args);a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob');assert counts(a)=={}
            root=post(a,'topic','body');child=post(a,'reply','body',root);deep=post(a,'deep','body',child)
            other=[post(a,f'other-{i}','body') for i in range(6)]
            assert counts(a)[root]==3 and len(counts(a))==7
            assert set(counts(g).values())=={None}
            read(b,root);assert counts(b)[root]==3
            mark(b,child);assert counts(b)[root]==2 and counts(a)[root]==3
            expect(a,f'HIDE {deep}',f'HIDDEN SAVED {deep}');assert counts(a)[root]==2 and counts(b)[root]==1
            expect(a,f'RESTORE {deep}',f'RESTORED {deep}');assert counts(b)[root]==2
            mark(b,child,False);assert counts(b)[root]==3
            for i in [root,child,deep]:mark(b,i)
            assert counts(b)[root]==0 and counts(b)[other[0]]==1
            expect(a,f'HIDE {root}',f'HIDDEN SAVED {root}');assert root not in counts(a)
            _,rows,_=page(a,'NOTES');assert rows[0].startswith(b'HIDDEN ') and b'unread=' not in rows[0]
            expect(a,f'RESTORE {root}',f'RESTORED {root}');assert counts(b)[root]==0
        finally:a.close();b.close();g.close();s.close()
        snapshot=(data.read_bytes(),users.read_bytes(),pathlib.Path(str(data)+'.moderation').read_bytes())
        s=Server(binary,data,**args,menu=True);a=Menu(s.port)
        try:
            a.until();a.cmd('LOGIN bob','> ');a.cmd(PASSWORD);out=a.cmd('NOTES')
            assert '未読: 0' in out and '未読: 1' in out and 'unread=' not in out
            assert 'other-5' in a.cmd('+')
        finally:a.close();s.close()
        s=Server(binary,data,**args,menu=True,telnet=True,language='en');a=TelnetClient(s.port)
        try:
            a.until();assert b'Unread: -' in a.cmd('NOTES')
            a.cmd('LOGIN alice',b'> ');a.cmd(PASSWORD);assert b'Unread: 3' in a.cmd('NOTES')
        finally:a.close();s.close()
        assert snapshot==(data.read_bytes(),users.read_bytes(),pathlib.Path(str(data)+'.moderation').read_bytes())
        print('PASS: parent/reply/deep counts, member isolation, explicit read state, hidden/restore, hidden parent placeholder, guests and multiple pages')
        print('PASS: restart, Japanese/English menus and Telnet, no implicit acknowledgement and byte-identical stores')

if __name__=='__main__':run(sys.argv[1])
