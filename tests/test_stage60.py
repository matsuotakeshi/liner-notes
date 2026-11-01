"""Stage60 board isolation, durable management, limits, import and retention."""
import pathlib,sys,tempfile,time,subprocess,importlib.util
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_stage58 import cmd,rows
ROOT=pathlib.Path(__file__).resolve().parents[1]
SECRET='Board-Mixed-Secret-60'

def password(c,board,value):
    time.sleep(1.05)
    assert cmd(c,f'BOARDPASS {board}')==b'NEW PASSWORD'
    assert cmd(c,value)==b'CONFIRM PASSWORD'
    assert cmd(c,value)==b'BOARD PASSWORD SAVED'

def enter(c,board,value=SECRET):
    time.sleep(1.05)
    assert cmd(c,f'BOARD {board}')==b'BOARD PASSWORD'
    assert cmd(c,value).startswith(f'BOARD {board} '.encode())

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='stage60-') as d:
        p=pathlib.Path(d);board=p/'notes';members=p/'members';admins=p/'admins'
        s=Server(binary,board,members=members)
        for name in ('admin','alice','bob'):
            c=Client(s.port);auth(c,name,True);c.close()
        s.close();admins.write_text('1\n');kw=dict(members=members,admins=admins)
        s=Server(binary,board,**kw);a,c,g=[Client(s.port) for _ in range(3)]
        try:
            auth(a,'admin');auth(c,'alice')
            for b in (2,3,4,5):assert cmd(a,f'boardset {b}|1|all|member|Board{b}|description')==f'BOARD UPDATED {b}'.encode()
            assert cmd(a,'BOARDOPTIONS 5 menu 1 0 0 3')==b'BOARD OPTIONS SAVED'
            assert b'MENU ORDER 1' in cmd(a,'BOARDOPTIONS 5')
            assert cmd(a,'BOARD 5').startswith(b'BOARD 5');assert cmd(a,'POST forbidden')==b'ERR posting denied'
            assert cmd(a,'BOARDSET 6|5|all|member|Child|child')==b'BOARD UPDATED 6'
            assert cmd(a,'BOARDDELETE 5 CONFIRM')==b'ERR board not empty'
            assert cmd(a,'BOARDDELETE 6 CONFIRM')==b'BOARD DELETED'
            assert cmd(a,'BOARDDELETE 5 CONFIRM')==b'BOARD DELETED'
            assert cmd(a,'BOARDSET 5|1|all|member|Reused|no')==b'ERR catalog syntax'
            assert cmd(c,'BOARD 2').startswith(b'BOARD 2');root=post(c,'Original Mixed','Body Mixed Case')
            reply=post(c,'Reply','Reply body',root)
            assert cmd(c,f'SEEN {root}').startswith(b'SEEN')
            assert cmd(c,'BOARDOPTIONS 2')==b'ERR admin required'
            assert cmd(a,'BOARDOPTIONS 2 MENU 1 0 0 1000')==b'ERR board not empty'
            assert cmd(a,'BOARDOPTIONS 2 BOARD 1 2 0 1000')==b'BOARD OPTIONS SAVED'
            assert cmd(c,'POST too many').startswith(b'EDIT');c.send(b'body\r\n.\r\n');assert c.line()==b'ERR board full'
            assert cmd(a,'BOARDOPTIONS 2 BOARD 1 0 1 1000')==b'BOARD OPTIONS SAVED'
            assert cmd(c,'POST too big').startswith(b'EDIT');c.send(b'body\r\n.\r\n');assert c.line()==b'ERR board full'
            assert cmd(a,'BOARDOPTIONS 2 BOARD 1 0 0 1000')==b'BOARD OPTIONS SAVED'
            password(a,2,SECRET)
            assert cmd(g,'READGLOBAL 1')==b'ERR no article'
            assert cmd(g,'INFO 2')==b'ERR board unavailable'
            assert b'Original' not in b'\n'.join(rows(g,'FIND ALL * *'))
            assert cmd(c,f'READ {root}')==b'ERR board unavailable'
            assert cmd(c,'BOARD 2')==b'BOARD PASSWORD';assert cmd(c,SECRET.lower())==b'ERR authentication'
            enter(c,2);assert read(c,root)[2]=='Body Mixed Case\r\n'
            peer=Client(s.port)
            try:
                auth(peer,'alice');assert cmd(peer,'READGLOBAL 1')==b'ERR no article'
            finally:peer.close()
            assert cmd(c,f'get {root}')==b'ARTICLE IMPORTED TO BUFFER'
            assert cmd(c,'BUFFER')==b'DOCUMENT EDIT'
            preview=rows(c,'/preview',b'END');assert b'Body Mixed Case' in b'\n'.join(preview)
            assert cmd(c,'/cancel')==b'CANCELLED'
            assert cmd(a,'INFOEDIT 2')==b'BOARD INFO EDIT'
            text='Mixed guide '+('x'*120)+' 日本語'
            for i in range(30):a.send((text+'\r\n').encode())
            assert cmd(a,'/save')==b'BOARD INFO SAVED'
            info=rows(c,'info 2',b'END INFO');assert sum(text.encode()==x for x in info)==30
            # A queued editor must not bypass a changed board password at commit.
            assert cmd(c,'POST denied after reset').startswith(b'EDIT');c.send(b'private draft\r\n')
            password(a,2,SECRET+'changed')
            assert cmd(c,'.')==b'ERR board unavailable'
            assert cmd(c,f'READ {root}')==b'ERR board unavailable'
            enter(c,2,SECRET+'changed')
            # Administrative reset bypasses only board passwords, not guest ACLs.
            assert cmd(a,'BOARD 2').startswith(b'BOARD 2')
            assert cmd(a,f'HIDE {root}').startswith(b'HIDDEN SAVED');assert cmd(a,f'RESTORE {root}').startswith(b'RESTORED')
            assert cmd(c,'MOVE 1 3 CONFIRM')==b'ERR admin required'
            assert cmd(a,f'MOVE {reply} 3 CONFIRM')==b'ERR move target'
            assert cmd(a,f'MOVE {root} 3 CONFIRM')==f'THREAD MOVED {root} 3'.encode()
            assert cmd(c,f'READ {root}')==b'ERR no article'
            assert cmd(c,'BOARD 3').startswith(b'BOARD 3');assert read(c,root)[2]=='Body Mixed Case\r\n'
            assert not any(x.startswith(f'#{root} '.encode()) for x in rows(c,'UNREAD'))
            third=post(c,'New reply','After move',reply)
            assert cmd(a,'BOARD 3').startswith(b'BOARD 3')
            assert cmd(a,f'MOVE {root} 4 CONFIRM')==f'THREAD MOVED {root} 4'.encode()
            assert cmd(a,'BOARDDELETE 2 CONFIRM')==b'BOARD DELETED'
            assert cmd(a,'BOARDDELETE 3 CONFIRM')==b'BOARD DELETED'
            assert cmd(a,'BOARDDELETE 1 CONFIRM')==b'ERR board not empty'
            assert cmd(a,'BOARDOPTIONS 4 BOARD 4 0 0 2')==b'BOARD OPTIONS SAVED'
            assert cmd(a,'BOARD 4').startswith(b'BOARD 4')
            for _ in range(70):assert read(a,root)[2]=='Body Mixed Case\r\n'
            log=rows(a,'BOARDACCESS 4');assert sum(x.startswith(b'ACCESSROW ') for x in log)==2
            assert b' R ' in b'\n'.join(log)
            # Automatic physical retention must also remove old records, not just hide them.
            logpath=pathlib.Path(str(board)+'.boardaccess')
            assert logpath.stat().st_size<=8+(2+63)*4096
            assert cmd(a,'BOARDOPTIONS 4 BOARD 4 0 0 2')==b'BOARD OPTIONS SAVED'
            assert logpath.stat().st_size==8+2*4096
            assert cmd(c,'BOARDACCESS 4')==b'ERR admin required'
            assert cmd(a,'BOARDSET 2|1|all|member|Reuse|no')==b'ERR catalog syntax'
        finally:
            for x in (a,c,g):x.close()
            s.close()
        assert SECRET.encode() not in pathlib.Path(str(board)+'.boardpolicy').read_bytes()
        assert board.read_bytes()[:8]==b'RTBOARD5'
        # Compaction preserves physical creation locations; move sidecar replays them.
        out=p/'compacted'
        result=subprocess.run([sys.executable,str(ROOT/'scripts/notes_compact.py'),'board',str(board),str(out)],capture_output=True,text=True)
        assert result.returncode==0,result.stderr
        board.write_bytes(out.read_bytes())
        spec=importlib.util.spec_from_file_location('retention',ROOT/'scripts/log-retain.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        log=pathlib.Path(str(board)+'.boardaccess');m.run(log,p/'retained',0)
        log.write_bytes((p/'retained/retained.dat').read_bytes())
        s=Server(binary,board,**kw);a,c=Client(s.port),Client(s.port)
        try:
            auth(a,'admin');auth(c,'alice')
            assert cmd(a,'BOARD 2')==b'ERR board unavailable'
            assert cmd(c,'BOARD 4').startswith(b'BOARD 4')
            assert read(c,third)[0]==reply;assert read(c,root)[1]=='Original Mixed'
            assert cmd(a,'BOARD 4').startswith(b'BOARD 4')
            assert b'RESTORE' in b'\n'.join(rows(a,'AUDIT'))
            assert len([x for x in rows(a,'BOARDACCESS 4') if x.startswith(b'ACCESSROW ')])==2
        finally:a.close();c.close();s.close()
        for suffix in ('.boardpolicy','.boardmoves','.boardaccess'):
            f=pathlib.Path(str(board)+suffix);data=f.read_bytes();f.write_bytes(data[:-1])
            result=subprocess.run([binary,'0',str(board),str(members)],capture_output=True,timeout=15)
            assert result.returncode and f.read_bytes()==data[:-1]
            f.write_bytes(data)
    print('PASS stage60: locked-board isolation/revision, long info, node policies/quotas, article import, move/reply/read-state/restart/compaction, delete/nonreuse, scoped retained access logs and corrupt-tail refusal')

if __name__=='__main__':run(sys.argv[1])
