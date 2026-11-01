"""Real Telnet upload/resume/cancel, connector replacement and auth isolation."""
import hashlib
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth, PASSWORD
from test_vault import downloader

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from transfer_connectors import LineConnector, Protocol
spec=importlib.util.spec_from_file_location('uploader',ROOT/'scripts/vault-upload.py')
uploader=importlib.util.module_from_spec(spec);spec.loader.exec_module(uploader)


def fails(call, text=None):
    try:call()
    except (ValueError,OSError) as error:
        if text:assert text.casefold() in str(error).casefold(),(text,str(error))
    else:raise AssertionError('operation unexpectedly succeeded')


def run(binary,ctl):
    binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
    with tempfile.TemporaryDirectory(prefix='network-transfer-') as d:
        p=pathlib.Path(d);vault=p/'vault';data=p/'bbs';users=p/'users';admins=p/'admins'
        subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
        s=Server(binary,data,members=users)
        try:
            for name in ['alice','bob','carol']:
                c=Client(s.port)
                try:auth(c,name,True)
                finally:c.close()
        finally:s.close()
        admins.write_text('1\n');args=dict(members=users,admins=admins,vault=vault)
        before={f:f.read_bytes() for f in p.iterdir() if f in (data,users)}
        source=p/'source.bin';payload=bytes(range(256))*20+b'\xff\x00END';source.write_bytes(payload)
        state=p/'upload.json';journal=vault/'index.dat';empty_hash=hashlib.sha256(b'').hexdigest()
        s=Server(binary,data,**args,menu=True,telnet=True);admin=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
        connect=lambda user:LineConnector('127.0.0.1',s.port,user,PASSWORD)
        def send(path=state,**kwargs):return uploader.upload('127.0.0.1',s.port,'bob',PASSWORD,source,path,**kwargs)
        def cancel(path):return uploader.cancel('127.0.0.1',s.port,'bob',PASSWORD,path)
        try:
            g=Protocol('127.0.0.1',s.port)
            try:
                g.request('RAW',lambda x:x==b'RAW')
                fails(lambda:g.request(f'FILESTART 0 {empty_hash} eA== -',lambda x:False),'login required')
            finally:g.close()
            def interrupt(done,total):
                if done>=672:raise OSError('test interruption')
            fails(lambda:send(progress=interrupt,name='日本語.bin',description='binary'),'test interruption')
            saved=json.loads(state.read_text(encoding='utf-8'));ident=saved['id']
            assert saved['phase']=='active' and PASSWORD not in state.read_text()
            carol=connect('carol')
            try:
                fails(lambda:carol.status(ident),'denied');fails(lambda:carol.write(ident,672,b'x'),'denied')
                fails(lambda:carol.finish(ident),'denied');fails(lambda:carol.cancel(ident),'denied')
            finally:carol.close()
            fails(lambda:admin.write(ident,672,b'x'),'denied')
            assert admin.status(ident)[1]==672
            fails(lambda:admin.info(ident),'no file')
            original=journal.read_bytes()
            fails(lambda:send(),'exists');assert journal.read_bytes()==original
            source.write_bytes(payload+b'x');fails(lambda:send(resume=True),'Source does not match');source.write_bytes(payload)
            fails(lambda:uploader.upload('wrong-host',s.port,'bob',PASSWORD,source,state,resume=True),'different endpoint')
            progress=[];result=send(resume=True,progress=lambda a,b:progress.append((a,b)))
            assert result==(ident,len(payload),hashlib.sha256(payload).hexdigest()) and progress[0][0]==672 and progress[-1][0]==len(payload)
            committed=journal.read_bytes();assert send(resume=True)==result and journal.read_bytes()==committed
            downloader.download('127.0.0.1',s.port,'carol',PASSWORD,ident,p/'received')
            assert (p/'received').read_bytes()==payload
            fails(lambda:cancel(state),'syntax')  # published file cannot be deleted by cancellation
            state2=p/'cancel.json';fails(lambda:send(state2,progress=interrupt),'test interruption');second=json.loads(state2.read_text())['id']
            cancel(state2);cancel(state2);assert not (vault/f'{second:08d}.part').exists()
            fails(lambda:send(state2,resume=True),'cannot be resumed')
            # Lost commit acknowledgement is recovered from the durable reservation.
            class LostCommit(LineConnector):
                def finish(self, ident):super().finish(ident);raise OSError('lost commit response')
            third=p/'commit.json';fails(lambda:send(third,connector_factory=LostCommit),'lost commit response')
            committed=journal.read_bytes();send(third,resume=True);assert journal.read_bytes()==committed
            # An uncertain START is never automatically retried, avoiding duplicates.
            class LostStart(LineConnector):
                last=None
                def begin(self,*args):LostStart.last=super().begin(*args);raise OSError('lost start response')
            uncertain=p/'uncertain.json';fails(lambda:send(uncertain,connector_factory=LostStart),'lost start response')
            unknown=journal.read_bytes();fails(lambda:send(uncertain,resume=True),'outcome is unknown');assert journal.read_bytes()==unknown
            admin.cancel(LostStart.last)
            # Suspension is rechecked on each block and on reauthentication.
            suspended=p/'suspended.json'
            def suspend(done,total):
                if done==672:admin.protocol.request('SUSPEND 2',lambda x:x==b'SUSPENDED 2')
            fails(lambda:send(suspended,progress=suspend))
            assert admin.status(json.loads(suspended.read_text())['id'])[1]==672
            fails(lambda:send(suspended,resume=True))
            admin.protocol.request('RESUME 2',lambda x:x==b'RESUMED 2');send(suspended,resume=True)
            # Malformed binary/metadata frames cannot mutate the journal.
            bad=connect('bob');prior=journal.read_bytes()
            try:
                for command in ['FILEPUT 1 0 /x==','FILEPUT 1 0 -',f'FILESTART 0 {empty_hash} AA== -',f'FILESTART 0 {empty_hash} Li4vZXNjYXBl -',f'FILESTART 0 {empty_hash} eA== Cg==']:
                    fails(lambda:bad.protocol.request(command,lambda x:False),'syntax')
                assert journal.read_bytes()==prior
            finally:bad.close()
            # Exercise the actual CLI and progress output without exposing password.
            empty=p/'empty';empty.write_bytes(b'')
            cli=subprocess.run([sys.executable,str(ROOT/'scripts/vault-upload.py'),'--host','127.0.0.1','--port',str(s.port),'--user','bob','--state',str(p/'cli.json'),'--password-stdin',str(empty)],input=PASSWORD+'\n',capture_output=True,text=True,timeout=20)
            assert cli.returncode==0 and '100%' in cli.stderr and 'Published file' in cli.stdout,(cli.stdout,cli.stderr)
            assert PASSWORD not in cli.stdout+cli.stderr+(p/'cli.json').read_text()
        finally:admin.close();s.close()
        assert all(f.read_bytes()==content for f,content in before.items())
        # A plain socket uses the same connector, independent of Telnet framing.
        s=Server(binary,data,**args)
        try:
            send(p/'plain.json');downloader.download('127.0.0.1',s.port,'bob',PASSWORD,ident,p/'plain-download')
        finally:s.close()
        print('PASS: real raw/Telnet transfer, resume/progress/cancel, metadata/hash validation, ambiguous START and lost commit handling, ownership/admin isolation, suspension and secret-free state')


if __name__=='__main__':run(sys.argv[1],sys.argv[2])
