import concurrent.futures
import pathlib
import subprocess
import sys
import tempfile
from test_runtime import Client
from test_board import Server,post,read
from test_members import auth,mark,unread
from test_notes import page

CONFIG='''# id|parent|read|write|title
1|0|all|member|Public
2|1|member|member|Members
3|2|u1|u1|Private
4|3|all|member|Private child
5|1|all|u1|Notices
6|1|all|none|Read only
7|0|all|member|Other
'''

def switch(c,id):
    c.send(f'BOARD {id}\n'.encode()); assert c.line().startswith(f'BOARD {id} '.encode())

def denied(c,cmd,message):
    c.send((cmd+'\n').encode()); c.expect(message)

def boards(c):
    result=[]; cursor=1
    while cursor:
        _,rows,cursor=page(c,f'BOARDS {cursor}'); result+=rows
    return result

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='catalog-') as d:
        p=pathlib.Path(d); data=p/'data'; users=p/'users'; config=p/'boards.conf'
        # Provision actual accounts before an ACL can grant their immutable IDs.
        s=Server(binary,data,members=users); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice',True); auth(b,'bob',True); assert post(a,'public','public body')==1
        finally: a.close(); b.close(); s.close()
        config.write_text(CONFIG,encoding='utf-8')
        s=Server(binary,data,members=users,catalog=config)
        a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'bob')
            a.send(b'ACCOUNT\n'); a.expect(b'ACCOUNT 1 alice')
            b.send(b'ACCOUNT\n'); b.expect(b'ACCOUNT 2 bob')
            assert [int(x.strip().split()[0][1:]) for x in boards(a)]==[1,2,3,4,5,6,7]
            assert [int(x.strip().split()[0][1:]) for x in boards(b)]==[1,2,5,6,7]
            assert [int(x.strip().split()[0][1:]) for x in boards(g)]==[1,5,6,7]
            for c in (b,g):
                denied(c,'BOARD 3',b'ERR board unavailable'); denied(c,'BOARD 4',b'ERR board unavailable')
            denied(g,'BOARD 2',b'ERR board unavailable')
            ids={}
            for board in [2,3,4,5,7]:
                switch(a,board); ids[board]=post(a,f'board-{board}',f'body-{board}')
            switch(a,6); denied(a,'POST forbidden',b'ERR posting denied')
            switch(b,5); denied(b,'POST forbidden',b'ERR posting denied')
            assert read(b,ids[5])[1]=='board-5'
            switch(b,1)
            for command in [f'READ {ids[3]}',f'TREE {ids[3]}',f'SEEN {ids[3]}',f'UNSEEN {ids[3]}',f'NEW {ids[3]}',f'UNREAD {ids[3]}']:
                denied(b,command,b'ERR no article')
            denied(b,f'REPLY {ids[3]} leak',b'ERR no parent')
            _,rows,_=page(b,'LIST'); assert len(rows)==1 and rows[0].startswith(b'1 ')
            _,rows,_=page(b,'NOTES'); assert len(rows)==1 and rows[0].startswith(b'#1 ')
            assert unread(b)==[1]
            switch(b,2); assert unread(b)==[ids[2]]; mark(b,ids[2]); assert unread(b)==[]
            switch(b,7); assert unread(b)==[ids[7]]
            denied(b,'PARENT',b'ERR no current article')
            switch(a,3); a.send(b'LOGOUT\n'); a.expect(b'LOGGED OUT')
            denied(a,f'READ {ids[3]}',b'ERR no article')
            a.close(); a=Client(s.port); auth(a,'alice')
            print('PASS: hierarchy visibility, ancestor read inheritance, read-only and named-ID posting ACLs')
            print('PASS: direct ID, reply, tree and read-state operations cannot cross current board')
            def add(i):
                c=Client(s.port)
                try:
                    auth(c,'bob'); switch(c,2 if i%2==0 else 7)
                    return post(c,f'parallel-{i}','body')
                finally: c.close()
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                created=list(pool.map(add,range(8)))
            assert len(set(created))==8
            switch(b,2); assert len(unread(b))==4
            switch(b,7); assert len(unread(b))==5
            print('PASS: concurrent posts stay in selected boards; unread sets follow board selection')
        finally: a.close(); b.close(); g.close(); s.close()
        s=Server(binary,data,members=users,catalog=config); b=Client(s.port)
        try:
            auth(b,'bob'); switch(b,2); assert len(unread(b))==4
            switch(b,7); assert len(unread(b))==5
        finally: b.close(); s.close()
        print('PASS: board membership and per-user read state survive restart')
        before=data.read_bytes(); user_before=users.read_bytes()
        invalid=[CONFIG+'2|0|all|all|Duplicate\n',CONFIG.replace('3|2|','3|4|'),
                 CONFIG.replace('u1','u999'),CONFIG.replace('2|1|','2|64|'),
                 CONFIG.replace('7|0|all|member|Other\n','')]
        for text in invalid:
            config.write_text(text,encoding='utf-8')
            r=subprocess.run([binary,'0',str(data),str(users),str(config)],capture_output=True,timeout=10)
            assert r.returncode!=0 and (b'catalog:' in r.stderr or b'board:' in r.stderr or b'board access:' in r.stderr or b'board policy:' in r.stderr),r.stderr
            assert data.read_bytes()==before and users.read_bytes()==user_before
        config.write_text(CONFIG.replace('3|2|u1|u1|','3|2|none|none|'),encoding='utf-8')
        s=Server(binary,data,members=users,catalog=config); a=Client(s.port)
        try:
            auth(a,'alice'); denied(a,'BOARD 3',b'ERR board unavailable'); denied(a,'BOARD 4',b'ERR board unavailable')
        finally: a.close(); s.close()
        print('PASS: invalid ACL/cycle/missing stored board rejected unchanged; restart applies revocation')

if __name__=='__main__': run(sys.argv[1])
