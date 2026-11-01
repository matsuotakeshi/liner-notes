from preference_assertions import assert_preferences_only
import pathlib,sys,tempfile,subprocess,struct,os,signal
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,mark,unread,PASSWORD
from test_catalog import switch
from test_notes import page,tree
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient
from test_list_pager import screens,command,MORE

def block(c,cmd,end):
    c.send((cmd+'\n').encode());rows=[]
    while True:
        row=c.line()
        if row==end:return rows
        assert not row.startswith(b'ERR'),row
        rows.append(row)

def article(c,cmd):
    c.send((cmd+'\n').encode());h=c.line();assert h.startswith(b'ARTICLE '),h
    title=c.line();n=int(c.line().split()[-1]);body=b''
    while n:
        if not c.buf:c.buf=c.sock.recv(8192)
        take=min(n,len(c.buf));body+=c.buf[:take];c.buf=c.buf[take:];n-=take
    c.expect(b'END');return int(h.split()[1]),title,body

def findall(c,query):
    cursor=1;result=[]
    while cursor:
        _,rows,cursor=page(c,f'FINDFROM {cursor} {query}');result+=rows
    return result

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    # Anonymous legacy author text must not become owned by a member named guest.
    with tempfile.TemporaryDirectory(prefix='anonymous-owner-') as d:
        p=pathlib.Path(d);data=p/'data';users=p/'users'
        s=Server(binary,data);c=Client(s.port)
        try:post(c,'anonymous','body')
        finally:c.close();s.close()
        s=Server(binary,data,members=users);c=Client(s.port)
        try:
            auth(c,'guest',True)
            expect(c,'WITHDRAW 1 CONFIRM','ERR article owner')
            expect(c,'RETITLE 1 stolen','ERR article owner')
        finally:c.close();s.close()
        print('PASS: reserved anonymous author text cannot be claimed by a same-name account')
    with tempfile.TemporaryDirectory(prefix='extended-') as d:
        p=pathlib.Path(d);data=p/'data';users=p/'users';cfg=p/'boards';admins=p/'admins';extra=p/'data.extras'
        s=Server(binary,data,members=users);a,b,m=Client(s.port),Client(s.port),Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True);auth(m,'mod',True)
        finally:a.close();b.close();m.close();s.close()
        cfg.write_text('1|0|all|member|Main|日本語の案内 FreeBSD\n2|1|u1|u1|Private|secret-information\n3|1|all|member|Other|other-information\n',encoding='utf-8');admins.write_text('3\n');args=dict(members=users,catalog=cfg,admins=admins)
        s=Server(binary,data,**args);a,b,m,g=[Client(s.port) for _ in range(4)]
        try:
            auth(a,'alice');auth(b,'bob');auth(m,'mod')
            root=post(a,'original','body-original');child=post(b,'child','child-body',root)
            expect(b,f'WITHDRAW {root} CONFIRM','ERR article owner');expect(m,f'WITHDRAW {root}',f'WITHDRAW CONFIRM {root}')
            expect(g,f'WITHDRAW {root} CONFIRM','ERR login required')
            expect(a,f'WITHDRAW {root}',f'WITHDRAW CONFIRM {root}');assert read(a,root)[1]=='original'
            expect(b,f'RETITLE {root} bad','ERR article owner')
            expect(a,f'RETITLE {root} 日本語-title',f'RETITLED {root}');assert read(b,root)[1]=='日本語-title'
            expect(m,f'RETITLE {root} admin-title',f'RETITLED {root}')
            assert findall(b,'ALL alice admin-title')[0].endswith(b'admin-title')
            expect(a,f'WITHDRAW {root} CONFIRM',f'WITHDRAW SAVED {root}')
            expect(a,f'WITHDRAW {root} CONFIRM',f'WITHDRAW SAVED {root}')
            expect(b,f'READ {root}',f'WITHDRAWN {root} PARENT 0')
            expect(m,f'INSPECT {root}',f'WITHDRAWN {root} PARENT 0')
            expect(m,f'RESTORE {root}','ERR article withdrawn')
            expect(a,f'RETITLE {root} blocked','ERR article hidden')
            expect(a,f'REPLY {root} blocked','ERR article hidden')
            rows=tree(b,root);assert rows[0].startswith(b'WITHDRAWN') and str(child).encode() in rows[1]
            assert root not in unread(b) and child in unread(b)
            assert not findall(b,'ALL * admin-title') and not findall(b,'ALL * body-original')
            switch(a,2);secret=post(a,'private-word','secret-body')
            switch(a,3);other=post(a,'other-title','needle');last=post(b,'main-title','needle')
            for i in range(10):post(b,f'many-{i}','needle')
            rows=findall(b,'ALL alice *');assert len(rows)==1 and rows[0].startswith(f'{other} 3 '.encode())
            assert all(b'private-word' not in row for row in findall(b,'ALL * *'))
            expect(b,f'READGLOBAL {secret}','ERR no article');expect(b,'INFO 2','ERR board unavailable')
            assert '日本語の案内'.encode() in block(b,'INFO',b'END INFO')[1]
            assert block(a,'INFO 2',b'END INFO')[1]==b'DESCRIPTION secret-information'
            expect(a,f'RETITLE {last} cross-board','ERR no article')
            expect(m,f'HIDE {last}',f'HIDDEN SAVED {last}')
            assert last not in [int(x.split()[0]) for x in findall(b,'ALL * needle')]
            before=unread(b)
            expected=[int(x.split()[0]) for x in findall(b,'ALL * needle')]
            got=[article(b,'SCAN ALL * needle')[0]]
            for _ in expected[1:]:got.append(article(b,'CONTINUE')[0])
            expect(b,'CONTINUE','SCAN END');assert got==expected
            switch(b,1);assert unread(b)==before
            article(b,'SCAN ALL alice *');b.send(b'STOP\n');assert b.line()=='連続読み出しを中止しました。'.encode()
            expect(b,'PING','PONG')
            assert article(a,'SCAN ALL * needle')[0]==other
            next_id=expected[1];expect(m,f'HIDE {next_id}',f'HIDDEN SAVED {next_id}')
            assert article(a,'CONTINUE')[0]==expected[2]
            a.send(b'STOP\n');a.line();expect(m,f'RESTORE {next_id}',f'RESTORED {next_id}')
            switch(a,3);expect(a,f'RETITLE {other} other-title-final',f'RETITLED {other}')
            expect(g,'PROFILE alice','ERR login required')
            expect(a,'HANDLE アリス','PROFILE SAVED');expect(a,'BIO FreeBSDと掲示板が好き','PROFILE SAVED')
            assert block(b,'PROFILE alice',b'END PROFILE')[1:] == ['HANDLE アリス'.encode(),'BIO FreeBSDと掲示板が好き'.encode(),b'LOCATION ',b'OCCUPATION ',b'INTERESTS ',b'WEBSITE ',b'CONTACT ']
            _,rows,_=page(b,'PROFILES 掲示板');assert rows[0].startswith(b'1 alice ')
            expect(b,'HANDLE Bob','PROFILE SAVED');expect(b,'BIO intro','PROFILE SAVED')
            expect(b,'BIO','PROFILE SAVED');assert block(a,'PROFILE bob',b'END PROFILE')[2]==b'BIO '
            expect(a,'BIO '+'x'*321,'ERR extended syntax')
            expect(m,'SUSPEND 2','SUSPENDED 2')
            expect(a,'PROFILE bob','ERR profile unavailable');assert all(b'bob' not in x for x in page(a,'PROFILES')[1])
            expect(m,'RESUME 2','RESUMED 2')
        finally:a.close();b.close();m.close();g.close();s.close(crash=True)
        original=extra.read_bytes();assert original.startswith(b'RTEXTRA1') and (len(original)-8)%512==0
        print('PASS: author withdrawal confirmation/ownership, preserved replies, no visibility/search/unread leaks, author/admin title edits and durable journal')
        print('PASS: cross-board and exact-author search, ACL/hidden exclusion, bounded result pages, sequential reading without marking, INFO and member profiles')
        # Restart persistence, profile page continuation and real menu/Telnet navigation.
        s=Server(binary,data,**args);a=Client(s.port)
        try:
            auth(a,'alice');expect(a,'READ 1','WITHDRAWN 1 PARENT 0')
            assert article(a,f'READGLOBAL {other}')[1]==b'TITLE other-title-final'
            switch(a,1)
            assert 'アリス'.encode() in block(a,'PROFILE',b'END PROFILE')[1]
            for i in range(8):
                c=Client(s.port)
                try:auth(c,f'user{i}',True)
                finally:c.close()
            h,rows,cursor=page(a,'PROFILES');assert len(rows)==8 and cursor
            assert len(page(a,f'PROFILESFROM {cursor} *')[1])==3
        finally:a.close();s.close()
        snapshot={f:f.read_bytes() for f in [data,users,extra,p/'data.moderation']}
        s=Server(binary,data,**args,menu=True,language='en');a=Menu(s.port)
        try:
            a.until();a.cmd('LOGIN bob','> ');a.cmd(PASSWORD)
            assert 'Information: 日本語の案内' in a.cmd('INFO')
            out=a.cmd('FIND ALL alice *');assert '[board 3' in out
            out=a.cmd(str(other));assert 'other-title' in out and 'needle' in out and 'Base notes' not in out
            assert 'Handle: アリス' in a.cmd('PROFILE alice')
            a.cmd('PROFILES');assert 'user7' in a.cmd('+')
            a.cmd('PROFILES 掲示板');assert 'Handle: アリス' in a.cmd('1')
            assert 'withdrawn by the author' in a.cmd('READGLOBAL 1')
            a.cmd('PAGER SIZE 80 7');a.cmd('PAGER ON')
            out,n=screens(a,'FIND ALL * needle');assert n>1
            out,n=screens(a,'+');assert b'many-9' in out
            a.cmd('PAGER OFF');out=a.cmd('SCAN ALL alice *');assert 'Sequential reading:' in out
            assert 'End of matching articles' in a.cmd('')
            a.cmd('PAGER ON');out,end=command(a,'SCAN ALL alice *');assert end==MORE
            out,end=command(a,'Q');assert end==b'] > '
            assert 'PONG' in a.cmd('PING');a.cmd('PAGER OFF')
            a.cmd('LANG ja');assert '自己紹介' in a.cmd('PROFILE alice')
            assert '横断・投稿者検索' in a.cmd('FIND ALL alice *')
            assert '案内: 日本語の案内' in a.cmd('INFO 1')
        finally:a.close();s.close()
        s=Server(binary,data,**args,menu=True,telnet=True,language='en');a=TelnetClient(s.port)
        try:
            a.until();a.cmd('LOGIN alice',b'> ');a.cmd(PASSWORD)
            assert b'Article #'+str(other).encode() in a.cmd('READGLOBAL '+str(other))
            assert b'Public profiles' in a.cmd('HELP PROFILE')
            assert b'Handle:' in a.cmd('PROFILE')
            assert b'Enter for the next article' in a.cmd('SCAN HERE alice *')
            assert b'stopped' in a.cmd('Q')
        finally:a.close();s.close()
        assert all(f.read_bytes()==v for f,v in snapshot.items() if f!=users)
        assert_preferences_only(snapshot[users],users.read_bytes())
        print('PASS: restart, public profile pagination, Japanese/English menus and Telnet, global result selection, paging, scan stop and read-only store invariance')
        # Corrupt and truncated journals must fail closed without repair.
        original=extra.read_bytes()
        for cut in [1,511]:
            extra.write_bytes(original[:-cut]);result=subprocess.run([binary,'0',str(data),str(users),str(cfg)],capture_output=True,timeout=10)
            assert result.returncode and b'extras:' in result.stderr and extra.read_bytes()==original[:-cut]
        for offset,val in [(1,1),(4,9999),(8,9999),(12,9999),(16,321)]:
            bad=bytearray(original);row=bytearray(bad[8:520]);struct.pack_into('<I',row,offset,val)
            h=2166136261
            for v in row[:508]:h=((h^v)*16777619)&0xffffffff
            struct.pack_into('<I',row,508,h);bad[8:520]=row;extra.write_bytes(bad)
            result=subprocess.run([binary,'0',str(data),str(users),str(cfg)],capture_output=True,timeout=10)
            assert result.returncode and extra.read_bytes()==bad
        extra.write_bytes(original)
        if os.name=='posix':
            import fault_io as resource
            limit=len(original)+256
            def fail_write():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,data,**args,preexec_fn=fail_write,options=["--user-log","-"]);a,g=Client(s.port),Client(s.port)
            try:
                auth(a,'alice');switch(a,3);expect(a,f'RETITLE {other} failure','ERR extras storage')
                expect(a,'PROFILE','ERR extras storage');expect(a,'READ 2','ERR extras storage')
                g.send(b'LOGIN bob\n'+PASSWORD.encode()+b'\n');g.expect(b'PASSWORD');g.expect(b'ERR extras storage')
                expect(a,'PING','PONG')
            finally:a.close();g.close();s.close()
            assert extra.stat().st_size==limit
            result=subprocess.run([binary,'0',str(data),str(users),str(cfg)],capture_output=True,timeout=10)
            assert result.returncode and b'extras:' in result.stderr
        print('PASS: malformed/rechecksummed and truncated extras rejected unchanged; POSIX partial write blocks access/authentication and restart')

if __name__=='__main__':run(sys.argv[1])
