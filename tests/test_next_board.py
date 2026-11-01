import pathlib,sys,tempfile
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,mark,PASSWORD
from test_catalog import switch
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='next-board-') as d:
        p=pathlib.Path(d); data=p/'data'; users=p/'users'; cfg=p/'boards'; admins=p/'admins'
        s=Server(binary,data,members=users); a,b=Client(s.port),Client(s.port)
        try: auth(a,'alice',True); auth(b,'bob',True)
        finally: a.close(); b.close(); s.close()
        # Display order is 1,2,5,3,4, not numeric ID order.
        cfg.write_text('1|0|all|member|Main\n2|1|all|member|Second\n3|1|all|u1|Read only for bob\n4|1|u1|u1|Private\n5|2|all|member|Child\n');admins.write_text('1\n')
        args=dict(members=users,catalog=cfg,admins=admins)
        s=Server(binary,data,**args);a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob');expect(g,'NEXTBOARD','ERR login required')
            expect(b,'NEXTBOARD','NEXTBOARD NONE')
            ids={}
            for board in [1,2,3,4,5]:
                switch(a,board);ids[board]=post(a,f'note-{board}','body')
            expect(b,'NEXTBOARD','BOARD 2 Second');expect(b,'NEXTBOARD','BOARD 5 Child')
            expect(b,'NEXTBOARD','BOARD 3 Read only for bob');expect(b,'NEXTBOARD','BOARD 1 Main')
            # Another member's seen state does not affect bob; hidden articles do.
            switch(a,2);mark(a,ids[2]);expect(a,f'HIDE {ids[2]}',f'HIDDEN SAVED {ids[2]}')
            expect(b,'NEXTBOARD','BOARD 5 Child')
            expect(a,f'RESTORE {ids[2]}',f'RESTORED {ids[2]}')
            switch(b,1);expect(b,'NEXTBOARD','BOARD 2 Second')
            switch(a,1);expect(a,'NEXTBOARD','BOARD 5 Child')
            for board in [2,5,3]:switch(b,board);mark(b,ids[board])
            switch(b,1);read(b,ids[1]);expect(b,'NEXTBOARD','NEXTBOARD NONE')
            expect(b,'SEEN',f'SEEN {ids[1]}')  # no target retains current article
            expect(b,'NEXTBOARD','NEXTBOARD NONE')
            switch(b,2);mark(b,ids[2],False);switch(b,1)
            expect(b,'NEXTBOARD','BOARD 2 Second');expect(b,'SEEN','ERR no article')
        finally:a.close();b.close();g.close();s.close()
        snapshot=(data.read_bytes(),users.read_bytes(),pathlib.Path(str(data)+'.moderation').read_bytes())
        s=Server(binary,data,**args,menu=True);a=Menu(s.port)
        try:
            assert 'B ' in a.until();a.cmd('LOGIN bob','> ');a.cmd(PASSWORD)
            out=a.cmd('nextboard');assert 'Second' in out and 'note-2' in out
            assert '他のボードに未読記事はありません' in a.cmd('NEXTBOARD')
        finally:a.close();s.close()
        s=Server(binary,data,**args,menu=True,telnet=True,language='en');a=TelnetClient(s.port)
        try:
            a.until();a.cmd('LOGIN bob',b'> ');a.cmd(PASSWORD)
            out=a.cmd('NEXTBOARD');assert b'Second' in out and b'note-2' in out
            assert b'No other board has unread articles' in a.cmd('NEXTBOARD')
        finally:a.close();s.close()
        assert snapshot==(data.read_bytes(),users.read_bytes(),pathlib.Path(str(data)+'.moderation').read_bytes())
        print('PASS: hierarchy order, wraparound, skip current/seen/hidden/inaccessible boards, read-only destination and per-member isolation')
        print('PASS: no-target state retention, successful selection reset, guest rejection, restart, Japanese/English menus and Telnet; stores unchanged')

if __name__=='__main__':run(sys.argv[1])
