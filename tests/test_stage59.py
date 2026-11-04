"""Stage59 real authorization boundaries, credential revocation, and idle handling."""
import hashlib,importlib.util,json,os,pathlib,shutil,subprocess,sys,tempfile,time
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_stage58 import cmd,rows,stats,counts,receive
from test_telnet import Client as Telnet
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from transfer_connectors import LineConnector

NEW='stage59-new-private-password'

def upload(port,name,category=''):
    c=LineConnector('127.0.0.1',port,name,PASSWORD)
    try:
        c.upload_category=category;data=b'owned data';ident=c.begin(len(data),hashlib.sha256(data).hexdigest(),'owned.bin','original')
        c.write(ident,0,data);c.finish(ident);return ident
    finally:c.close()

def closed(c):
    try:
        while c.sock.recv(8192):pass
    except (ConnectionResetError,ConnectionAbortedError):pass

def run(binary,ctl):
    binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
    with tempfile.TemporaryDirectory(prefix='stage59-') as directory:
        p=pathlib.Path(directory);board=p/'notes';members=p/'members';admins=p/'admins';vault=p/'vault'
        s=Server(binary,board,members=members)
        for name in ('admin','sigop','owner','other'):
            c=Client(s.port);auth(c,name,True);c.close()
        s.close();admins.write_text('1\n');subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
        options=dict(members=members,admins=admins,vault=vault)
        s=Server(binary,board,**options);a,b,c,d=[Client(s.port) for _ in range(4)]
        try:
            for conn,name in zip((a,b,c,d),('admin','sigop','owner','other')):auth(conn,name)
            assert cmd(a,'BOARDSET 2|1|all|member|Second|two')==b'BOARD UPDATED 2'
            assert cmd(a,'BOARDSET 3|2|all|member|Child|three')==b'BOARD UPDATED 3'
            assert cmd(c,'BOARD 2').startswith(b'BOARD 2');article=post(c,'owned','original body')
            assert cmd(c,'BOARD 3').startswith(b'BOARD 3');child=post(c,'child','child body')
            assert cmd(b,'SIGOP 2 2')==b'ERR catalog syntax'
            assert b'SIGOP 2 2' not in b'\n'.join(rows(a,'SIGOPS',b'END SIGOPS'))
            assert cmd(a,'SIGOP 2 2')==b'SIGOP SET 2 2';closed(b);b.close();b=Client(s.port);auth(b,'sigop')
            assert b'SIGOP 2 2' in rows(b,'SIGOPS',b'END SIGOPS')
            assert cmd(b,'BOARD 2').startswith(b'BOARD 2')
            assert cmd(b,f'HIDE {article}').startswith(b'HIDDEN SAVED')
            assert cmd(b,f'RESTORE {article}').startswith(b'RESTORED')
            assert cmd(b,f'RETITLE {article} changed')==f'RETITLED {article}'.encode()
            assert 'changed' in read(b,article)[1]
            assert b'HIDE' in b'\n'.join(rows(b,'AUDIT'))
            assert b'Child' in cmd(b,'BOARDINFO 3');assert cmd(b,'BOARDINFO 1')==b'ERR admin required'
            assert cmd(b,'BOARDSET 2|3|all|member|Cycle|no')==b'ERR catalog syntax'
            assert cmd(b,'BOARDSET 2|0|all|member|Moved|no')==b'ERR catalog syntax'
            assert cmd(b,'BOARDSET 4|2|all|member|New|no')==b'BOARD UPDATED 4'
            assert cmd(b,'BOARDSET 5|1|all|member|Outside|no')==b'ERR catalog syntax'
            assert cmd(b,'BOARDSET 2|1|none|none|Locked|only sigop')==b'BOARD UPDATED 2'
            assert b'Locked' in cmd(b,'BOARDINFO 2');assert read(b,article)[2]=='original body\r\n'
            assert cmd(d,'BOARD 2')==b'ERR board unavailable'
            assert cmd(b,'BOARDSET 2|1|all|member|Second|restored')==b'BOARD UPDATED 2'
            assert cmd(b,'BOARD 3').startswith(b'BOARD 3');assert cmd(b,f'HIDE {child}')==f'HIDDEN SAVED {child}'.encode();assert cmd(b,f'RESTORE {child}')==f'RESTORED {child}'.encode()
            assert cmd(b,'RESETPASS 3')==b'ERR admin required'
            for text in ('SUSPEND 3','LEVEL 3 5','ANNOUNCE no','SHUTDOWN NOW CONFIRM'):
                assert cmd(b,text)==b'ERR admin required',text
            ident=upload(s.port,'owner')
            assert cmd(d,f'FILEDESC {ident} illegal')==b'ERR vault denied'
            assert cmd(d,f'FILEDELETE {ident} CONFIRM')==b'ERR vault denied'
            assert cmd(c,f'FILEDESC {ident} owner edit')==f'FILE EDITED {ident}'.encode()
            assert cmd(a,'FILEBOARDSET -|member|none')==b'FILEBOARD UPDATED'
            assert cmd(c,f'FILEDESC {ident} blocked')==b'ERR vault denied'
            assert cmd(c,f'FILEDELETE {ident} CONFIRM')==b'ERR vault denied'
            assert cmd(a,'FILEBOARDSET -|member|member')==b'FILEBOARD UPDATED'
            x=LineConnector('127.0.0.1',s.port,'other',PASSWORD)
            try:receive(x,ident);x.complete_download(ident)
            finally:x.close()
            assert counts(a,ident)==1
            assert cmd(c,f'FILEDELETE {ident}')==b'ERR vault syntax'
            assert cmd(c,f'FILEDELETE {ident} CONFIRM')==f'FILE DELETED {ident}'.encode()
            assert not (vault/f'{ident:08d}.bin').exists();assert counts(a,ident)==1
            assert cmd(c,f'FILEDELETE {ident} CONFIRM')==f'FILE DELETED {ident}'.encode()
            assert cmd(a,f'FILERESTORE {ident} CONFIRM')==b'ERR no file'
            # Faster worker wakeups can reach this before the one-second
            # post-login credential throttle. Test authorization after it.
            time.sleep(1.05)
            reply=cmd(a,'RESETPASS 1');assert reply==b'ERR protected member',reply
            assert cmd(a,'RESETPASS 3')==b'NEW PASSWORD'
            assert cmd(a,NEW)==b'CONFIRM PASSWORD';assert cmd(a,'wrong-confirmation')==b'ERR password mismatch'
            time.sleep(1.05);assert cmd(a,'RESETPASS 3')==b'NEW PASSWORD';assert cmd(a,'/CANCEL')==b'CANCELLED'
            time.sleep(1.05);assert cmd(a,'RESETPASS 3')==b'NEW PASSWORD'
            assert cmd(a,NEW)==b'CONFIRM PASSWORD';assert cmd(a,NEW)==b'PASSWORD RESET 3';closed(c)
            c.close();c=Client(s.port);auth(c,'owner',password=NEW)
            assert cmd(a,'SIGOP 2 NONE')==b'SIGOP SET 2 0';closed(b);b.close();b=Client(s.port);auth(b,'sigop')
            assert cmd(b,'BOARD 2').startswith(b'BOARD 2');assert cmd(b,f'HIDE {article}')==b'ERR admin required'
            assert cmd(a,'SIGOP 2 2')==b'SIGOP SET 2 2'
        finally:
            for x in (a,b,c,d):x.close()
            s.close()
        assert NEW.encode() not in members.read_bytes()
        s=Server(binary,board,**options);b,c=Client(s.port),Client(s.port)
        try:
            auth(b,'sigop');auth(c,'owner',password=NEW)
            assert cmd(b,'BOARD 2').startswith(b'BOARD 2');assert cmd(b,f'HIDE {article}').startswith(b'HIDDEN SAVED')
            assert cmd(c,'BOARD 2').startswith(b'BOARD 2');assert cmd(c,f'READ {article}').startswith(b'HIDDEN ')
        finally:b.close();c.close();s.close()
        # Idle applies to guests, authenticated users, and unfinished secret input.
        s=Server(binary,board,**options,options=['--idle-seconds','2']);c=Client(s.port)
        try:
            c.send(b'ECH');time.sleep(1.1);c.send(b'O active\n');assert c.line()==b'active'
            time.sleep(1.1);assert cmd(c,'PING')==b'PONG'
            assert cmd(c,'LOGIN owner')==b'PASSWORD';c.sock.settimeout(5)
            assert c.line()==b'BYE idle timeout';closed(c)
        finally:c.close();s.close()
        s=Server(binary,board,**options,options=['--idle-seconds','0']);c=Client(s.port)
        try:time.sleep(2.3);assert cmd(c,'PING')==b'PONG'
        finally:c.close();s.close()
        # Telnet reset never echoes the new secret; scoped help/menu must be visible.
        s=Server(binary,board,**options,menu=True,telnet=True,language='en');a,b=Telnet(s.port),Telnet(s.port)
        try:
            for x,name in ((a,'admin'),(b,'sigop')):
                x.until();x.cmd('LOGIN '+name,b'> ');x.cmd(PASSWORD)
            b.cmd('BOARD 2');assert b'Board SIGOP' in b.cmd('MENU')
            assert b'HIDE' in b.cmd('HELP SIGOP');assert b'RESETPASS' not in b.cmd('MENU')
            b.cmd('BOARD 3');assert b'Board SIGOP' in b.cmd('MENU')
            time.sleep(1.05);result=a.cmd('RESETPASS 4',b'> ');assert b'new password' in result,result
            assert NEW.encode() not in a.cmd(NEW,b'> ')
            assert NEW.encode() not in a.cmd(NEW)
            a.cmd('LANG ja');time.sleep(1.05)
            assert '新しいパスワード'.encode() in a.cmd('RESETPASS 3',b'> ')
            a.cmd(NEW,b'> ');assert '再設定'.encode() in a.cmd(NEW)
        finally:a.close();b.close();s.close()
        # Truncated role and credential journals must fail closed without rewriting.
        for path in (pathlib.Path(str(board)+'.sigops'),members):
            original=path.read_bytes();path.write_bytes(original[:-1])
            result=subprocess.run([binary,'--admins',str(admins),'0',str(board),str(members)],capture_output=True,timeout=10)
            assert result.returncode and path.read_bytes()==original[:-1]
            path.write_bytes(original)
        if os.name=='posix':
            import fault_io as resource
            import signal
            original=members.read_bytes();limit=len(original)+64
            def size_limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,board,members=members,admins=admins,preexec_fn=size_limit,options=['--user-log','-'])
            a=Client(s.port)
            try:
                auth(a,'admin');time.sleep(1.05)
                assert cmd(a,'RESETPASS 4')==b'NEW PASSWORD'
                assert cmd(a,NEW)==b'CONFIRM PASSWORD'
                assert cmd(a,NEW)==b'ERR member storage unavailable'
                assert cmd(a,'READ 1')==b'ERR member storage unavailable'
                assert cmd(a,'PING')==b'PONG'
            finally:a.close();s.close()
            assert members.stat().st_size==limit
            result=subprocess.run([binary,'0',str(board),str(members)],capture_output=True,timeout=10)
            assert result.returncode and members.stat().st_size==limit
            print('PASS stage59: reset short write fails closed; truncated credential/role journals refused')
    print('PASS stage59: ownership/policy, scoped SIGOP, revocation/restart, secret password reset and idle disconnect')

if __name__=='__main__':run(*sys.argv[1:])
