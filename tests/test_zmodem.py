"""Real lrzsz interoperability on FreeBSD/POSIX, including runtime publication.

Usage: python3 tests/test_zmodem.py ./runtime ./vaultctl /prefix/bin/lrz /prefix/bin/lsz
"""
import hashlib
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time
from test_board import Server
from test_runtime import Client
from test_members import auth, PASSWORD
from test_zmodem_workflows import fails
from transfer_connectors import LineConnector
import vault_zmodem as gateway
from zmodem_connector import ZmodemProcess, TransferFailure


def peer(executable, arguments, directory, action):
    a, b = socket.socketpair()
    proc = subprocess.Popen([executable, '--binary', '--quiet', '--escape', '--syslog=off']+arguments,
                            cwd=directory, stdin=b, stdout=b, stderr=subprocess.PIPE)
    b.close()
    try:
        result = action(a.fileno())
        assert proc.wait(timeout=5) == 0, proc.stderr.read()
        return result
    finally:
        a.close()
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=2)
            except subprocess.TimeoutExpired: proc.kill(); proc.wait()
        proc.stderr.close()


def run(binary, ctl, receiver, sender):
    binary, ctl, receiver, sender = [str(pathlib.Path(x).resolve()) for x in (binary, ctl, receiver, sender)]
    engine = ZmodemProcess(receiver, sender, timeout=15)
    engine.executable(receiver); engine.executable(sender)
    with tempfile.TemporaryDirectory(prefix='zmodem-test-') as directory:
        p = pathlib.Path(directory); vault=p/'vault'; users=p/'users'; data=p/'bbs'; admins=p/'admins'
        subprocess.run([ctl, 'init', str(vault)], check=True, capture_output=True)
        s=Server(binary,data,members=users,vault=vault)
        try:
            for name in ('alice','bob'):
                c=Client(s.port)
                try: auth(c,name,True)
                finally: c.close()
        finally: s.close()
        admins.write_text('1\n')
        s=Server(binary,data,members=users,vault=vault,admins=admins)
        c=LineConnector('127.0.0.1',s.port,'bob',PASSWORD)
        admin=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
        spool=p/'spool'; spool.mkdir(); origin=p/'origin'; origin.mkdir(); target=p/'target'; target.mkdir()
        try:
            for number,payload in enumerate((bytes(range(256))*257+b'\r\n\0\xff\x1b[2J', b'')):
                filename=f'payload-{number}.bin'; (origin/filename).write_bytes(payload)
                receipt=peer(sender,['--',filename],origin,
                    lambda fd: gateway.receive(c,engine,'127.0.0.1',s.port,'bob',spool,fd,fd))
                ident,size,digest=receipt
                assert (size,digest)==(len(payload),hashlib.sha256(payload).hexdigest())
                peer(receiver,['--restricted'],target,
                    lambda fd: gateway.send(c,engine,ident,spool,fd,fd,name=filename))
                assert (target/filename).read_bytes()==payload
                assert not list(spool.iterdir())
                # A receiver skipping an existing file must not be called success.
                fails(lambda: peer(receiver,['--restricted','--protect'],target,
                    lambda fd: gateway.send(c,engine,ident,spool,fd,fd,name=filename)))
            journal=(vault/'index.dat').read_bytes()
            # Batches must never publish even their first successfully received file.
            fails(lambda:peer(sender,['--','payload-0.bin','payload-1.bin'],origin,
                lambda fd:gateway.receive(c,engine,'127.0.0.1',s.port,'bob',spool,fd,fd)))
            assert (vault/'index.dat').read_bytes()==journal and not list(spool.iterdir())
            outside=p/'outside.bin';outside.write_bytes(b'unchanged source')
            fails(lambda:peer(sender,['--full-path','--','../outside.bin'],origin,
                lambda fd:gateway.receive(c,engine,'127.0.0.1',s.port,'bob',spool,fd,fd)))
            assert outside.read_bytes()==b'unchanged source'
            assert (vault/'index.dat').read_bytes()==journal and not list(spool.iterdir())
            with (origin/'oversize.bin').open('wb') as f:f.truncate(1024*1024*1024+1024)
            fails(lambda:peer(sender,['--','oversize.bin'],origin,
                lambda fd:gateway.receive(c,engine,'127.0.0.1',s.port,'bob',spool,fd,fd)))
            assert (vault/'index.dat').read_bytes()==journal and not list(spool.iterdir())
            a,b=socket.socketpair()
            try:
                b.sendall(b'\x18'*8+b'\x08'*8);b.shutdown(socket.SHUT_WR)
                fails(lambda:gateway.receive(c,engine,'127.0.0.1',s.port,'bob',spool,a.fileno(),a.fileno()))
            finally:a.close();b.close()
            assert (vault/'index.dat').read_bytes()==journal and not list(spool.iterdir())
            # Complete CLI path, with a separate secret FD and pure protocol stdout.
            a,b=socket.socketpair(); secret_r,secret_w=os.pipe()
            os.write(secret_w,(PASSWORD+'\n').encode());os.close(secret_w)
            script=pathlib.Path(__file__).resolve().parents[1]/'scripts/vault_zmodem.py'
            command=[sys.executable,str(script),'receive','--host','127.0.0.1','--port',str(s.port),
                     '--user','bob','--receiver',receiver,'--sender',sender,'--lang','en',
                     '--password-fd',str(secret_r),'--spool-root',str(spool)]
            entry=subprocess.Popen(command,stdin=a,stdout=a,stderr=subprocess.PIPE,pass_fds=(secret_r,))
            os.close(secret_r);a.close()
            partner=subprocess.Popen([sender,'--quiet','--binary','--syslog=off','--','payload-0.bin'],
                                     cwd=origin,stdin=b,stdout=b,stderr=subprocess.PIPE)
            b.close()
            try:
                assert partner.wait(timeout=20)==0,partner.stderr.read()
                assert entry.wait(timeout=20)==0,entry.stderr.read()
                notice=entry.stderr.read();assert b'Published file #' in notice and PASSWORD.encode() not in notice
            finally:
                for child in (entry,partner):
                    if child.poll() is None:child.kill();child.wait()
                    child.stderr.close()
            assert not list(spool.iterdir())
            journal=(vault/'index.dat').read_bytes()
            # Timeout restores terminal flags and reaps the receiver process.
            import pty,termios
            master,slave=pty.openpty();before=termios.tcgetattr(slave)
            try:
                started=time.monotonic()
                fails(lambda:gateway.receive(c,ZmodemProcess(receiver,sender,1),'127.0.0.1',s.port,'bob',spool,slave,slave))
                assert time.monotonic()-started<4 and termios.tcgetattr(slave)==before
            finally:os.close(master);os.close(slave)
            assert not list(spool.iterdir())
            # Revoke the actual BBS session while the ZMODEM child is running.
            class Revoke(ZmodemProcess):
                def receive(self,directory,stdin,stdout,authorize):
                    count=0
                    def checked():
                        nonlocal count
                        count+=1
                        if count==2:admin.protocol.request('SUSPEND 2',lambda x:x==b'SUSPENDED 2')
                        authorize()
                    return super().receive(directory,stdin,stdout,checked)
            a,b=socket.socketpair()
            try:
                fails(lambda:gateway.receive(c,Revoke(receiver,sender,5),'127.0.0.1',s.port,'bob',spool,a.fileno(),a.fileno()))
            finally:a.close();b.close()
            assert (vault/'index.dat').read_bytes()==journal and not list(spool.iterdir())
        finally:c.close();admin.close();s.close()
    print('PASS: real lrzsz 0.13.1 send/receive, all-byte/empty roundtrip, SHA-256 publication, skipped files, batch/cancel/timeout rejection, PTY restoration and live member revocation')


if __name__=='__main__':run(*sys.argv[1:])
