import pathlib,sys,tempfile,subprocess,os,signal,struct
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='prefs-') as d:
        p=pathlib.Path(d); data=p/'data'; users=p/'users'
        s=Server(binary,data,members=users); a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            expect(g,'PREFS SAVE','ERR login required')
            auth(a,'alice',True); auth(b,'bob',True)
            expect(a,'PREFS','PREFS NONE'); expect(a,'PREFS LOAD','PREFS NONE')
            a.send(b'LANG en\n'); assert b'en' in a.line()
            expect(a,'PAGER ON','PAGER ON'); expect(a,'PREFS SAVE','PREFS SAVED en ON')
            expect(a,'PREFS SAVE','PREFS SAVED en ON')
            expect(b,'PREFS SAVE','PREFS SAVED ja OFF')
            expect(a,'PAGER OFF','PAGER OFF'); expect(a,'PREFS','PREFS STORED en ON')
            expect(a,'PREFS LOAD','PREFS LOADED en ON'); expect(a,'PAGER','PAGER ON')
            auth(g,'alice'); expect(g,'PAGER','PAGER ON')
            expect(g,'PAGER OFF','PAGER OFF'); expect(g,'PREFS SAVE','PREFS SAVED en OFF')
            expect(a,'PAGER','PAGER ON'); expect(a,'PREFS LOAD','PREFS LOADED en OFF')
            post(a,'kept article','unchanged body')
        finally: a.close(); b.close(); g.close(); s.close()
        raw=users.read_bytes(); board=data.read_bytes()
        assert sum(raw[i]==ord('f') for i in range(8,len(raw),128))==3
        assert PASSWORD.encode() not in raw
        print('PASS: guest rejection, missing defaults, explicit save/load, duplicate suppression and multi-session isolation')
        s=Server(binary,data,members=users,menu=True,language='ja'); a=Menu(s.port)
        try:
            a.until(); a.cmd('LOGIN alice','> ')
            assert 'Logged in as alice' in a.cmd(PASSWORD)
            assert 'OFF' in a.cmd('PAGER')
            a.cmd('PAGER ON'); assert 'saved' in a.cmd('PREFS SAVE')
        finally: a.close(); s.close()
        s=Server(binary,data,members=users,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('LOGIN bob',b'> ')
            assert 'ログインしました'.encode() in a.cmd(PASSWORD)
            assert b'OFF' in a.cmd('PAGER')
            assert '保存済み設定'.encode() in a.cmd('PREFS')
        finally: a.close(); s.close()
        assert data.read_bytes()==board
        raw=users.read_bytes()
        print('PASS: restart and Telnet restore each account language before login response; article bytes preserved')
        for field,value in [(4,999),(8,2),(9,2),(10,2),(11,16),(12,19),(16,101)]:
            bad=bytearray(raw); row=bytearray(bad[-128:]); row[field]=value if value<256 else 255
            h=2166136261
            for byte in row[:124]: h=((h^byte)*16777619)&0xffffffff
            struct.pack_into('<I',row,124,h); bad[-128:]=row; users.write_bytes(bad)
            result=subprocess.run([binary,'0',str(data),str(users)],capture_output=True,timeout=10)
            assert result.returncode and users.read_bytes()==bad
        users.write_bytes(raw[:-1]); result=subprocess.run([binary,'0',str(data),str(users)],capture_output=True,timeout=10)
        assert result.returncode and users.read_bytes()==raw[:-1]; users.write_bytes(raw)
        print('PASS: invalid member/language/pager/reserved bytes and truncated records rejected without rewrite')
        if os.name=='posix':
            import fault_io as resource
            limit=len(raw)+64
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN); resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,data,members=users,preexec_fn=size_limit,options=["--user-log","-"]); a,g=Client(s.port),Client(s.port)
            try:
                auth(a,'alice'); expect(a,'PAGER OFF','PAGER OFF')
                expect(a,'PREFS SAVE','ERR member storage unavailable')
                expect(a,'PREFS LOAD','ERR member storage unavailable')
                expect(a,'READ 1','ERR member storage unavailable')
                g.send(b'LOGIN alice\n'+PASSWORD.encode()+b'\n'); g.expect(b'PASSWORD'); g.expect(b'ERR member storage unavailable')
                expect(a,'PING','PONG')
            finally:
                a.close(); g.close()
                # The injected short write also makes disconnect autosave fail.
                s.proc.terminate();s.proc.wait(timeout=5)
                diagnostic=s.proc.stderr.read()
                assert diagnostic=='disconnect preferences: ERR member storage unavailable\n',diagnostic
            assert users.stat().st_size==limit and data.read_bytes()==board
            result=subprocess.run([binary,'0',str(data),str(users)],capture_output=True,timeout=10)
            assert result.returncode and users.stat().st_size==limit
            print('PASS: actual short-write never acknowledges save; board/auth fail closed and truncated restart refused')

if __name__=='__main__': run(sys.argv[1])
