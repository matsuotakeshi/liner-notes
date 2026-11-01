"""Reconcile real public/private reservations, including a lost start reply."""
from pathlib import Path
import hashlib,json,subprocess,sys,tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from transfer_connectors import LineConnector,MailConnector,RemoteError
from transfer_spool import reconcile


def write_private(path,value):
    path.write_text(json.dumps(value));path.chmod(0o600)


def scenario(base,port,factory):
    spool=base/factory.name;spool.mkdir(mode=0o700)
    identity=['127.0.0.1',port,'alice',factory.name,'']
    key=hashlib.sha256(json.dumps(identity).encode()).hexdigest()
    work=spool/('pending-'+key);work.mkdir(mode=0o700)
    (work/'lock').touch(mode=0o600);(work/'data').mkdir(mode=0o700)
    owner=factory('127.0.0.1',port,'alice',PASSWORD)
    stranger=factory('127.0.0.1',port,'bob',PASSWORD)
    manifest={};states=[];identifiers=[]
    try:
        for index in range(2):
            name=f'file{index}.txt';payload=('payload'+str(index)).encode()
            source=work/'data'/name;source.write_bytes(payload);source.chmod(0o600)
            digest=hashlib.sha256(payload).hexdigest();manifest[name]=[len(payload),digest]
            ident=owner.begin(len(payload),digest,name,'reconcile test');identifiers.append(ident)
            owner.write(ident,0,payload)
            if index==0:owner.finish(ident)
            state=work/(hashlib.sha256(name.encode()).hexdigest()+'.json');states.append(state)
            write_private(state,dict(version=1,host=identity[0],port=port,user='alice',connector=factory.name,source=str(source),size=len(payload),sha256=digest,name=name,description='reconcile test',category='',id=None if index==0 else ident,phase='starting' if index==0 else 'active'))
        write_private(work/'transfer.json',dict(identity=identity,received=True,batch=True,description='reconcile test',manifest=manifest))
        args=(spool,work.name,identity[0],port,'alice',PASSWORD)
        bindings=['file0.txt='+str(identifiers[0])]
        original=[p.read_bytes() for p in states]
        report=reconcile(*args,bindings=bindings)
        assert [f['remote'] for f in report['files']]==['COMMITTED','ACTIVE']
        assert original==[p.read_bytes() for p in states]
        try:reconcile(*args,bindings=bindings,apply=True,cleanup=True)
        except ValueError:pass
        else:raise AssertionError('Active reservation was cleaned')
        assert original==[p.read_bytes() for p in states]
        try:stranger.status(identifiers[1])
        except RemoteError:pass
        else:raise AssertionError('Other member could inspect reservation')
        report=reconcile(*args,bindings=bindings,apply=True)
        assert not report['all_committed'] and json.loads(states[0].read_bytes())['id']==identifiers[0]
        owner.finish(identifiers[1])
        report=reconcile(*args,apply=True,cleanup=True)
        assert report['all_committed'] and not work.exists()
        for ident in identifiers:assert owner.status(ident)[0]=='COMMITTED'
    finally:owner.close();stranger.close()


if __name__=='__main__':
    binary,ctl=map(lambda x:str(Path(x).resolve()),sys.argv[1:3])
    with tempfile.TemporaryDirectory(prefix='reconcile71-') as tmp:
        p=Path(tmp);vault=p/'vault';subprocess.run([ctl,'init',vault],check=True,capture_output=True)
        s=Server(binary,p/'notes',members=p/'members',vault=vault)
        try:
            for user in ('alice','bob'):
                c=Client(s.port)
                try:auth(c,user,True)
                finally:c.close()
            for factory in (LineConnector,MailConnector):scenario(p,s.port,factory)
        finally:s.close()
    print('PASS real public/private reservation reconcile, lost reply binding, active cleanup refusal, owner isolation and published payload preservation')
