"""Deterministic blocked hash: BBS stays responsive; revocation prevents publication."""
import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile
import threading
import time
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_network_transfer import fails
from transfer_connectors import BinaryConnector,LineConnector


def run(binary,ctl):
    if os.name!='posix':
        print('SKIP: LD_PRELOAD worker isolation requires POSIX');return
    binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
    with tempfile.TemporaryDirectory(prefix='worker-isolation-') as d:
        p=pathlib.Path(d);vault=p/'vault';users=p/'users';data=p/'bbs';admins=p/'admins';gate=p/'gate';marker=p/'marker'
        shim=p/'slow.so'
        subprocess.run(['cc','-shared','-fPIC','-pthread',str(pathlib.Path(__file__).with_name('slow_vault_io.c')),'-o',str(shim)],check=True)
        subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
        s=Server(binary,data,members=users,vault=vault)
        try:
            for name in ('alice','bob'):
                c=Client(s.port)
                try:auth(c,name,True)
                finally:c.close()
            c=BinaryConnector('127.0.0.1',s.port,'bob',PASSWORD)
            payload=bytes(range(256))*2;digest=hashlib.sha256(payload).hexdigest()
            ids=[]
            for _ in range(2):
                ident=c.begin(len(payload),digest,'data.bin','worker test');c.write(ident,0,payload);ids.append(ident)
            c.close()
        finally:s.close()
        admins.write_text('1\n')
        for ident,disconnect in zip(ids,(False,True)):
            gate.touch();marker.unlink(missing_ok=True)
            env={'LD_PRELOAD':str(shim),'SLOW_VAULT_INODE':str((vault/f'{ident:08d}.part').stat().st_ino),'SLOW_VAULT_GATE':str(gate),'SLOW_VAULT_MARKER':str(marker)}
            previous={k:os.environ.get(k) for k in env};os.environ.update(env)
            try:s=Server(binary,data,members=users,vault=vault,admins=admins)
            finally:
                for k,v in previous.items():
                    if v is None:os.environ.pop(k,None)
                    else:os.environ[k]=v
            admin=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
            c=BinaryConnector('127.0.0.1',s.port,'bob',PASSWORD);reader=Client(s.port);outcome=[]
            def finish():
                try:outcome.append(c.finish(ident))
                except (ValueError,OSError):outcome.append('rejected')
            t=threading.Thread(target=finish)
            try:
                auth(reader,'alice');t.start()
                deadline=time.monotonic()+5
                while not marker.exists() and time.monotonic()<deadline:time.sleep(.01)
                assert marker.exists(),'hash did not reach gate'
                start=time.monotonic();article=post(reader,'worker remains available','hello');assert read(reader,article)[2].strip()=='hello'
                assert time.monotonic()-start<2,'BBS blocked by vault hashing'
                if disconnect:
                    import socket
                    c.protocol.wire.sock.shutdown(socket.SHUT_RDWR);c.close();time.sleep(.2)
                else:admin.protocol.request('SUSPEND 2',lambda x:x==b'SUSPENDED 2')
                gate.unlink();t.join(5);assert not t.is_alive() and outcome==['rejected'],outcome
                assert admin.status(ident)[0]=='ACTIVE';fails(lambda:admin.info(ident),'no file')
                if not disconnect:admin.protocol.request('RESUME 2',lambda x:x==b'RESUMED 2')
                resumed=BinaryConnector('127.0.0.1',s.port,'bob',PASSWORD)
                try:assert resumed.finish(ident)==(len(payload),digest)
                finally:resumed.close()
            finally:
                gate.unlink(missing_ok=True)
                if t.ident:t.join(5)
                reader.close();c.close();admin.close();s.close()
    print('PASS: blocked vault hash allows BBS read/write; suspend/disconnect deny publication; authorized retry succeeds')

if __name__=='__main__':run(*sys.argv[1:])
