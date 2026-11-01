from preference_assertions import assert_preferences_only
import pathlib,sys,tempfile
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,mark,PASSWORD
from test_notes import page
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient
from test_list_pager import screens

def filtered(c):
    ids=[];cursor=1
    while cursor:
        h,rows,cursor=page(c,f'NOTES UNREAD {cursor}')
        assert h.startswith(b'NOTES UNREAD ')
        ids.extend(int(row.split()[0][1:]) for row in rows)
    return ids

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='unread-threads-') as d:
        p=pathlib.Path(d);data=p/'data';users=p/'users';admins=p/'admins'
        s=Server(binary,data,members=users);a=Client(s.port)
        try:auth(a,'alice',True)
        finally:a.close();s.close()
        admins.write_text('1\n');args=dict(members=users,admins=admins)
        s=Server(binary,data,**args);a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob',True);expect(g,'NOTES UNREAD','ERR login required')
            assert filtered(a)==[]
            roots=[post(a,f'topic-{i}','body') for i in range(8)]
            child=post(a,'reply','body',roots[0]);deep=post(a,'deep','body',child)
            mark(a,roots[0]);mark(a,child);mark(a,roots[1])
            assert filtered(a)==[roots[0]]+roots[2:] and filtered(b)==roots
            h,rows,cursor=page(a,'NOTES UNREAD');assert h==b'NOTES UNREAD 7' and len(rows)==5 and cursor==7
            expect(a,f'HIDE {deep}',f'HIDDEN SAVED {deep}');assert filtered(a)==roots[2:]
            expect(a,f'RESTORE {deep}',f'RESTORED {deep}');assert filtered(a)[0]==roots[0]
            expect(a,f'HIDE {roots[2]}',f'HIDDEN SAVED {roots[2]}');assert roots[2] not in filtered(a)
            expect(a,f'RESTORE {roots[2]}',f'RESTORED {roots[2]}')
            expect(a,'NOTES UNREAD 0','ERR article number');expect(a,'NOTES UNREAD 1 extra','ERR article number')
            read(a,roots[0]);filtered(a);expect(a,'SEEN',f'SEEN {roots[0]}')
        finally:a.close();b.close();g.close();s.close()
        snapshot=(data.read_bytes(),users.read_bytes(),pathlib.Path(str(data)+'.moderation').read_bytes())
        s=Server(binary,data,**args,menu=True,language='en');a=Menu(s.port);b=Menu(s.port)
        try:
            a.until();b.until();a.cmd('LOGIN alice','> ');a.cmd(PASSWORD)
            out=a.cmd('NOTES UNREAD');assert 'Threads with unread articles: 7' in out and 'topic-1' not in out
            out=a.cmd('+');assert 'topic-6' in out and 'topic-7' in out and 'unread articles' in out
            assert 'topic-1' in a.cmd('NOTES')
            assert 'topic-1' in b.cmd('NOTES')  # independent guest session
            a.cmd('PAGER SIZE 80 7');a.cmd('PAGER ON')
            out,n=screens(a,'NOTES UNREAD');assert n>1 and b'topic-1' not in out
            out,n=screens(a,'+');assert b'topic-7' in out
            a.cmd('PAGER OFF');a.cmd('LANG ja');assert '未読のあるスレッド' in a.cmd('notes unread')
            assert 'topic-0' in a.cmd('1')  # filtered numeric selection reads an article
        finally:a.close();b.close();s.close()
        s=Server(binary,data,**args,menu=True,telnet=True,language='en');a=TelnetClient(s.port)
        try:
            a.until();a.cmd('LOGIN alice',b'> ');a.cmd(PASSWORD);a.cmd('LANG en');a.cmd('PAGER OFF')
            assert b'Threads with unread articles: 7' in a.cmd('NOTES UNREAD')
            assert b'topic-7' in a.cmd('+');assert b'topic-1' in a.cmd('NOTES')
        finally:a.close();s.close()
        assert snapshot[0]==data.read_bytes() and snapshot[2]==pathlib.Path(str(data)+'.moderation').read_bytes()
        assert_preferences_only(snapshot[1],users.read_bytes())
        s=Server(binary,data,**args);a=Client(s.port)
        try:
            auth(a,'alice')
            for i in roots+[child,deep]:mark(a,i)
            assert page(a,'NOTES UNREAD')==(b'NOTES UNREAD 0',[],0)
            assert len(page(a,'NOTES')[1])==5
        finally:a.close();s.close()
        print('PASS: unread-only membership, deep replies, per-member isolation, hide/restore, hidden roots, empty/all-seen, strict cursor and retained current article')
        print('PASS: filtered pagination, N resets, independent sessions, Japanese/English, PAGER ON/OFF, numeric selection, Telnet and byte-identical stores')

if __name__=='__main__':run(sys.argv[1])
