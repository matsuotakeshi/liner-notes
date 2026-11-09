"""Actual lrzsz over the runtime Telnet menu; no password in the helper."""
from test_zmodem_workflows import spool_contents
import hashlib,os,pathlib,select,shlex,socket,subprocess,sys,tempfile,time
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet
from transfer_connectors import LineConnector


def transfer(c,executable,args,directory,base_options=None,prompt=b'] > ',deadline_seconds=40,send_rate=None):
    left,right=socket.socketpair()
    proc=subprocess.Popen([executable]+(['--binary','--quiet','--escape','--syslog=off'] if base_options is None else base_options)+args,cwd=directory,stdin=right,stdout=right,stderr=subprocess.PIPE)
    right.close();left.setblocking(False);pending=bytearray(c.buf);c.buf=b'';deadline=time.monotonic()+deadline_seconds
    try:
        while proc.poll() is None:
            if time.monotonic()>deadline:raise AssertionError('peer timed out')
            reads,writes,_=select.select([left,c.sock],[left] if pending else [],[],.02)
            if left in writes:
                try:n=left.send(pending);del pending[:n]
                except BlockingIOError:pass
            if left in reads:
                data=left.recv(65536)
                if data:
                    c.send(data.replace(b'\xff',b'\xff\xff'))
                    if send_rate:time.sleep(len(data)/send_rate)
            if c.sock in reads:c.receive();pending.extend(c.buf);c.buf=b''
        # Drain the final OO/ack bytes emitted immediately before process exit.
        while select.select([left],[],[],.05)[0]:
            data=left.recv(65536)
            if not data:break
            c.send(data.replace(b'\xff',b'\xff\xff'))
        c.buf=bytes(pending)+c.buf
        return proc.returncode,c.until(prompt),proc.stderr.read()
    finally:
        left.close()
        if proc.poll() is None:proc.kill();proc.wait()
        proc.stderr.close()


def run(binary,ctl,receiver,sender,rights=False):
    def failed(output):
        if b'Transfer timed out' in output:
            s.expected_errors.append('ZMODEM failure: TIMEOUT');return True
        return b'failed or cancelled' in output
    binary,ctl,receiver,sender=[str(pathlib.Path(x).resolve()) for x in (binary,ctl,receiver,sender)]
    root=pathlib.Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='zmenu-') as directory:
        p=pathlib.Path(directory);vault=p/'vault';data=p/'bbs';members=p/'members';spool=p/'spool';spool.mkdir();source=p/'source';source.mkdir();target=p/'target';target.mkdir()
        subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
        helper=p/'helper';helper.write_text('#!/bin/sh\nexec '+shlex.join([sys.executable,str(root/'scripts/zmodem_host.py'),'--receiver',receiver,'--sender',sender,'--spool-root',str(spool),'--timeout','5'])+' -- "$@"\n');helper.chmod(0o700)
        s=Server(binary,data,members=members,vault=vault);a,b=Client(s.port),Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True)
        finally:a.close();b.close();s.close()
        admins=p/'admins';admins.write_text('1\n')
        s=Server(binary,data,members=members,vault=vault,zhelper=helper,admins=admins,menu=True,telnet=True,language='en');c=Telnet(s.port);guest=Telnet(s.port)
        api=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
        def downloaded(ident):
            api.protocol.send(f'FILESTATS {ident}');count=None
            while True:
                row=api.protocol.line()
                if row.startswith(b'DOWNLOADCOUNT '):count=int(row.split()[2])
                if row.startswith(b'END NEXT '):break
            return count
        try:
            guest.until();assert b'log in' in guest.cmd('ZUPLOAD').lower();guest.close()
            c.until();c.cmd('LOGIN bob',b'> ');c.cmd(PASSWORD);assert b'Vault (' in c.cmd('VAULT')
            for n,payload in enumerate([bytes(range(256))*257+b'\r\n\0\xff\x1b[2J',b'',b'UTF-8 filename']):
                name=f'payload-{n}.bin' if n<2 else '日本語 ファイル.bin';(source/name).write_bytes(payload)
                c.cmd('U',b'> ');c.cmd('ZMODEM',b'> ');c.cmd('test-description',b'1 GiB.\r\n')
                code,out,err=transfer(c,sender,['--',name],source);assert code==0 and b'published' in out,(code,out,err)
                ident=n+1;assert api.info(ident)==(len(payload),hashlib.sha256(payload).hexdigest())
                c.cmd(str(ident),b'0:back > ');c.cmd('Z',b'1 GiB.\r\n')
                code,out,err=transfer(c,receiver,['--restricted','--protect'],target,prompt=b'0:back > ');c.cmd('0');assert code==0 and b'sent (' in out,(code,out,err)
                assert (target/name).read_bytes()==payload
                assert downloaded(ident)==1
                assert b'PONG' in c.cmd('PING')
            assert not spool_contents(spool)
            batch_target=p/'batch-target';batch_target.mkdir()
            for ident in [1,2,3,1]:assert b'Selected' in c.cmd('BATCH ADD '+str(ident))
            assert b'3 files: 1,2,3' in c.cmd('BATCH LIST')
            c.cmd('BATCH REMOVE 2');c.cmd('BATCH ADD 2')
            c.cmd('BATCH SEND',b'16 GiB total.\r\n')
            code,out,err=transfer(c,receiver,['--restricted','--protect'],batch_target)
            assert code==0 and b'Batch sent: 3 files' in out,(code,out,err)
            for path in source.iterdir():assert (batch_target/path.name).read_bytes()==path.read_bytes()
            assert [downloaded(i) for i in (1,2,3)]==[2,2,2]
            assert b'BATCH COMPLETE' in c.cmd('BATCH SEND')
            c.cmd('BATCH CLEAR')
            for ident in (1,2,3):c.cmd('BATCH ADD '+str(ident))
            c.cmd('BATCH SEND',b'16 GiB total.\r\n')
            code,out,err=transfer(c,receiver,['--restricted','--protect'],batch_target)
            assert failed(out)
            c.cmd('BATCH SEND',b'16 GiB total.\r\n');c.send(b'\x18'*8+b'\x08'*8)
            assert failed(c.until())
            assert [downloaded(i) for i in (1,2,3)]==[2,2,2]  # skip/cancel must never count
            assert b'Selected 0' in c.cmd('BATCH CLEAR')
            assert b'BATCH ADD/REMOVE' in c.cmd('BATCH SEND')
            assert not spool_contents(spool)
            c.cmd('D 1',b'1 GiB.\r\n');code,out,err=transfer(c,receiver,['--restricted','--protect'],target);assert failed(out)
            c.cmd('U',b'> ');c.cmd('ZMODEM',b'> ');c.cmd('',b'1 GiB.\r\n');c.send(b'\x18'*8+b'\x08'*8);assert failed(c.until())
            assert not spool_contents(spool)
            c.cmd('U',b'> ');c.cmd('ZMODEM',b'> ');c.cmd('',b'1 GiB.\r\n');code,out,err=transfer(c,sender,['--','payload-0.bin','payload-1.bin'],source);assert failed(out)
            assert not spool_contents(spool)
            # A private-talk attempt while the engine owns the wire is refused.
            api.protocol.send('ONLINE');target=None
            for _ in range(32):
                row=api.protocol.line()
                if row==b'CHAT END':break
                if row.startswith(b'CHAT WHO ') and b'bob' in row:target=int(row.split()[2])
            assert target
            c.cmd('U',b'> ');c.cmd('ZMODEM',b'> ');c.cmd('',b'1 GiB.\r\n')
            api.protocol.request('TALK '+str(target),lambda x:x.startswith(b'CHAT TALK OPEN '))
            api.protocol.request('/SAY secret-during-transfer',lambda x:x.startswith(b'CHAT ERROR target'))
            api.protocol.request('//',lambda x:x==b'TALK CLOSED')
            s.expected_errors.append('ZMODEM failure: TIMEOUT')
            out=c.until();assert b'Transfer timed out' in out and b'secret-during-transfer' not in out # configured engine timeout
            assert b'PONG' in c.cmd('PING')
            # The global cap is checked before launching a ninth engine.
            waiting=[]
            try:
                for _ in range(8):
                    x=Telnet(s.port);waiting.append(x);x.until();x.cmd('LOGIN bob',b'> ');x.cmd(PASSWORD)
                for x in waiting:x.cmd('ZUPLOAD',b'1 GiB.\r\n')
                c.cmd('U',b'> ');c.cmd('ZMODEM',b'> ');c.cmd('',b'1 GiB.\r\n');assert failed(c.until())
                for x in waiting:x.send(b'\x18'*8+b'\x08'*8)
                for x in waiting:assert failed(x.until())
            finally:
                for x in waiting:x.close()
            assert b'test-description'   in c.cmd('FILE 1');assert b'payload-0' in c.cmd('S payload-0');c.cmd('C');c.cmd('0')
            api.protocol.request('FILEHIDE 1 CONFIRM',lambda x:x==b'FILE HIDDEN 1')
            c.cmd('ZDOWNLOAD 1',b'1 GiB.\r\n');assert failed(c.until())
            c.cmd('LANG ja');assert '保管庫'.encode() in c.cmd('VAULT');c.cmd('0')
            # Board-targeted reception is private from the first publication event.
            for path,read,write in [('-', 'member','member'),('private','member','none'),('private/docs','member','member')]:
                subprocess.run([ctl,'board-set',str(vault),path,read,write],check=True,capture_output=True)
            c.cmd('LANG en');c.cmd('VAULT')
            assert b'private/docs' in c.cmd('FILEBOARDS')
            c.cmd('UIN private/docs|board description',b'1 GiB.\r\n')
            code,out,err=transfer(c,sender,['--','payload-0.bin'],source);assert code==0 and b'published' in out,(code,out,err)
            assert b'payload-0.bin' in c.cmd('FILESIN private')
            c.cmd('UIN private|denied',b'1 GiB.\r\n');assert failed(c.until())
            assert not spool_contents(spool)
            c.cmd('0');c.cmd('LANG ja')
            # Authorization changes close the bound transfer; no partial upload becomes public.
            c.cmd('ZUPLOAD', '1GiBまで。\r\n'.encode());api.protocol.request('LEVEL 2 1' if rights else 'SUSPEND 2',lambda x:x.startswith(b'RIGHTS' if rights else b'SUSPENDED'))
            deadline=time.monotonic()+5
            while time.monotonic()<deadline:
                if not c.sock.recv(4096):break
            else:raise AssertionError('revoked connection did not close')
        finally:c.close();api.close();s.close()
        time.sleep(.1);assert not spool_contents(spool),spool_contents(spool)
    print('PASS: menu ZMODEM upload/download all bytes and empty files, original names, SHA-256, listing/search, JP/EN, skip/cancel, guest denial, revocation and spool cleanup')
if __name__=='__main__':run(*sys.argv[1:])
