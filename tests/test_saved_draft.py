import pathlib,sys,tempfile,subprocess,os,signal,struct
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD,unread
from test_moderation import expect
from test_preview import preview
from test_ui import Menu
from test_telnet import Client as TelnetClient

def records(raw):
    offset=8
    while offset<len(raw):
        size=1152 if raw[offset]==ord('D') else 128
        yield offset,raw[offset:offset+size]; offset+=size
    assert offset==len(raw)

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='saved-draft-') as d:
        p=pathlib.Path(d); board=p/'board'; users=p/'users'; config=p/'boards'; admins=p/'admins'
        s=Server(binary,board,members=users); a,b=Client(s.port),Client(s.port)
        try: auth(a,'alice',True); auth(b,'bob',True)
        finally: a.close(); b.close(); s.close()
        config.write_text('1|0|all|member|Main\n2|1|u1|u1|Private\n'); admins.write_text('1\n')
        args=dict(members=users,catalog=config,admins=admins)
        s=Server(binary,board,**args); a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            expect(g,'RESUME','ERR login required'); auth(a,'alice'); auth(b,'alice'); expect(a,'DRAFT','DRAFT NONE')
            expect(a,'RESUME','DRAFT NONE'); expect(a,'BOARD 2','BOARD 2 Private'); assert post(a,'parent','body')==1
            before=unread(a)
            expect(a,'REPLY 1 日本語','EDIT end with . or /CANCEL'); a.send('本文\n//SAVE\n'.encode()); expect(a,'/SAVE','DRAFT STORED 1')
            expect(b,'RESUME','DRAFT RESUMED'); h,t,body=preview(b); assert b'PARENT 1' in h and body=='本文\r\n/SAVE\r\n'.encode()
            expect(b,'/UNDO','ERR no undo')
            a.send(b'newer\n'); expect(a,'/SAVE','DRAFT STORED 2')
            b.send(b'stale\n'); expect(b,'/SAVE','ERR draft conflict'); assert b'stale' in preview(b)[2]
            expect(b,'/CANCEL','CANCELLED'); expect(b,'DRAFT DELETE 1','ERR draft conflict'); expect(b,'DRAFT','DRAFT STORED 2')
            expect(b,'RESUME','DRAFT RESUMED'); assert preview(b)[2].endswith(b'newer\r\n'); expect(b,'/CANCEL','CANCELLED')
            auth(g,'bob'); expect(g,'DRAFT','DRAFT NONE'); expect(g,'DRAFT DELETE 2','ERR draft conflict')
            expect(a,'/CANCEL','CANCELLED'); assert unread(a)==before
        finally: s.close(crash=True); a.close(); b.close(); g.close()
        raw=users.read_bytes(); snapshot=board.read_bytes()
        assert len([r for _,r in records(raw) if r[0]==ord('D')])==2
        print('PASS: member/board isolation, save/resume, UTF-8 and escape, stale-session conflict, exact revision delete and unchanged unread state')
        s=Server(binary,board,**args); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'alice'); expect(a,'DRAFT','DRAFT STORED 2')
            expect(a,'RESUME','DRAFT RESUMED'); assert preview(a)[2].endswith(b'newer\r\n')
            expect(b,'BOARD 2','BOARD 2 Private'); expect(b,'HIDE 1','HIDDEN SAVED 1')
            expect(a,'/SAVE','ERR article hidden'); expect(a,'.','ERR article hidden')
            expect(a,'RESUME','ERR article hidden'); expect(b,'RESTORE 1','RESTORED 1')
            expect(a,'RESUME','DRAFT RESUMED'); expect(a,'.','SAVED 2'); expect(a,'DRAFT','DRAFT STORED 2')
            assert read(b,2)[1:] == ('日本語','本文\r\n/SAVE\r\nnewer\r\n')
        finally: a.close(); b.close(); s.close()
        config.write_text('1|0|all|member|Main\n2|1|u2|u2|Private\n')
        s=Server(binary,board,**args); a=Client(s.port)
        try:
            auth(a,'alice'); expect(a,'RESUME','ERR board unavailable'); expect(a,'DRAFT','DRAFT STORED 2')
            expect(a,'DRAFT DELETE 0','ERR draft revision'); expect(a,'DRAFT DELETE 2','DRAFT REMOVED')
            expect(a,'RESUME','DRAFT NONE')
        finally: a.close(); s.close()
        config.write_text('1|0|all|member|Main\n2|1|u1|u1|Private\n')
        print('PASS: forced restart retains saved draft; resume/save/post recheck hidden parent and ACL; explicit removal works after ACL loss')
        s=Server(binary,board,**args,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('LOGIN alice',b'> '); a.cmd(PASSWORD); a.cmd('POST empty',b'Body > ')
            assert b'revision 4' in a.cmd('/SAVE',b'Body > '); a.cmd('/CANCEL')
            assert b'Draft resumed' in a.cmd('RESUME',b'Body > ')
            a.cmd('x'*1022,b'Body > '); assert b'revision 5' in a.cmd('/SAVE',b'Body > '); a.cmd('/CANCEL')
            a.cmd('LANG ja'); assert '再開しました'.encode() in a.cmd('RESUME','本文 > '.encode())
            assert '保存下書き'.encode() in a.cmd('/SAVE','本文 > '.encode()); a.cmd('/CANCEL')
        finally: a.close(); s.close()
        raw=users.read_bytes(); snapshot=board.read_bytes(); drows=[(offset,row) for offset,row in records(raw) if row[0]==ord('D')]
        assert len(drows[-1][1])==1152
        # Validate record framing, reserved bytes, lengths and checksums without repairing corrupt input.
        offset,row=drows[-1]
        for field,value in [(4,999),(16,999),(20,81),(24,1025),(28,1)]:
            bad=bytearray(raw); part=bytearray(row); struct.pack_into('<I',part,field,value)
            h=2166136261
            for byte in part[:1148]: h=((h^byte)*16777619)&0xffffffff
            struct.pack_into('<I',part,1148,h); bad[offset:offset+1152]=part; users.write_bytes(bad)
            result=subprocess.run([binary,'0',str(board),str(users)],capture_output=True,timeout=10)
            assert result.returncode and users.read_bytes()==bad
        for cut in [1,1025]:
            users.write_bytes(raw[:-cut]); result=subprocess.run([binary,'0',str(board),str(users)],capture_output=True,timeout=10)
            assert result.returncode and users.read_bytes()==raw[:-cut]
        users.write_bytes(raw)
        print('PASS: empty/max-size drafts, revision sequence after deletion, English/Japanese Telnet, corrupt/truncated journal rejected unchanged')
        if os.name=='posix':
            import fault_io as resource
            limit=len(raw)+576
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN); resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,board,**args,preexec_fn=size_limit,options=["--user-log","-"]); a,g=Client(s.port),Client(s.port)
            try:
                auth(a,'alice'); expect(a,'RESUME','DRAFT RESUMED')
                expect(a,'/SAVE','ERR member storage unavailable'); expect(a,'/CANCEL','CANCELLED')
                expect(a,'DRAFT','ERR member storage unavailable')
                g.send(b'LOGIN alice\n'+PASSWORD.encode()+b'\n'); g.expect(b'PASSWORD'); g.expect(b'ERR member storage unavailable')
                expect(a,'PING','PONG')
            finally: a.close(); g.close(); s.close()
            assert users.stat().st_size==limit and board.read_bytes()==snapshot
            result=subprocess.run([binary,'0',str(board),str(users)],capture_output=True,timeout=10)
            assert result.returncode and users.stat().st_size==limit
            print('PASS: actual partial draft write never acknowledges success; subsequent member operations/auth stop and restart rejects partial record')

if __name__=='__main__': run(sys.argv[1])
