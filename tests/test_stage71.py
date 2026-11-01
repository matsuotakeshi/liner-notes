from pathlib import Path
import hashlib,importlib.util,json,os,sqlite3,subprocess,sys,tempfile
from unittest.mock import patch
from test_board import Server,post,read
from test_runtime import Client
from test_telnet import Client as Telnet
from test_members import auth,PASSWORD
from test_stage58 import cmd,rows
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import transfer_spool,transfer_connectors,upgrade_check

def receipt_checks(base):
    spec=importlib.util.spec_from_file_location('resumecheck',ROOT/'ports/test-resume.py')
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    p=base/'receipt.json';first=dict(patch='crc32-lp64-v1',sha256='a'*64)
    assert mod.save_receipt(p,first)=='created';original=p.read_bytes()
    assert mod.save_receipt(p,first)=='unchanged' and p.read_bytes()==original
    second=dict(first,sha256='b'*64)
    with patch.object(mod.os,'replace',side_effect=OSError('injected rename failure')):
        try:mod.save_receipt(p,second)
        except OSError:pass
        else:raise AssertionError('Expected failure')
    assert p.read_bytes()==original
    assert mod.save_receipt(p,second)=='updated'
    assert json.loads(p.read_bytes())==second
    assert all(x.read_bytes()==original for x in base.glob('receipt.json.bak-*'))
    link=base/'linked.json';link.symlink_to(p)
    try:mod.save_receipt(link,first)
    except OSError:pass
    else:raise AssertionError('Symlink accepted')
    asset=base/'help.txt';asset.write_text('custom');asset.with_name('help.txt.sample').write_text('new')
    assert upgrade_check.assets([base])[0]['status']=='review-required'
    assert asset.read_text()=='custom'
    print('PASS repeatable receipt creation/reuse/backup/atomic failure/symlink and non-mutating asset check')

def spool_checks(base):
    spool=base/'spool';spool.mkdir(mode=0o700)
    identity=['127.0.0.1',2222,'alice','line-v1','docs']
    key=hashlib.sha256(json.dumps(identity).encode()).hexdigest()
    work=spool/('pending-'+key);work.mkdir(mode=0o700);(work/'lock').touch(mode=0o600)
    data=work/'data';data.mkdir(mode=0o700);source=data/'test.txt';source.write_bytes(b'complete');source.chmod(0o600)
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    meta={'identity':identity,'received':True,'batch':False,'description':'example','manifest':{'test.txt':[8,digest]}}
    (work/'transfer.json').write_text(json.dumps(meta));(work/'transfer.json').chmod(0o600)
    state=work/(hashlib.sha256(b'test.txt').hexdigest()+'.json')
    state.write_text(json.dumps(dict(version=1,host=identity[0],port=2222,user='alice',connector='line-v1',source=str(source),size=8,sha256=digest,name='test.txt',description='example',category='docs',id=None,phase='starting')));state.chmod(0o600)
    class Fake:
        name='line-v1'
        def __init__(self,*args):assert args[-1]=='test-secret'
        def status(self,ident):assert ident==7;return 'COMMITTED',8,8,digest
        def close(self):pass
    original=state.read_bytes()
    with patch.dict(transfer_connectors.CONNECTORS,{'line-v1':Fake}):
        args=(spool,work.name,'127.0.0.1',2222,'alice','test-secret')
        out=transfer_spool.reconcile(*args,bindings=['test.txt=7'])
        assert out['all_committed'] and state.read_bytes()==original
        with patch.object(Fake,'status',return_value=('ACTIVE',4,8,digest)):
            try:transfer_spool.reconcile(*args,bindings=['test.txt=7'],apply=True,cleanup=True)
            except ValueError:pass
            else:raise AssertionError('Unfinished publication was cleaned')
        assert state.read_bytes()==original and source.read_bytes()==b'complete'
        transfer_spool.reconcile(*args,bindings=['test.txt=7'],apply=True)
        assert json.loads(state.read_bytes())['id']==7
        try:transfer_spool.reconcile(spool,work.name,'evil.example',2222,'alice','test-secret')
        except ValueError:pass
        else:raise AssertionError('Endpoint substitution accepted')
        transfer_spool.reconcile(*args,apply=True,cleanup=True)
        assert not work.exists()
    print('PASS spool detail, explicit lost-ID binding, status reconciliation, preview, endpoint isolation and confirmed-only cleanup')

def database_reader(binary,base):
    db=base/'liner.sqlite';options=['--database-structured',str(db)]
    admins=base/'admins';admins.write_text('1\n')
    s=Server(binary,base/'notes',members=base/'members',options=options)
    a=Client(s.port);b=Client(s.port)
    try:
        auth(a,'alice',True);auth(b,'bob',True)
        a.close();b.close();s.close()
        s=Server(binary,base/'notes',members=base/'members',options=options,admins=admins)
        a=Client(s.port);b=Client(s.port);auth(a,'alice');auth(b,'bob')
        assert cmd(a,'PROFILESET HANDLE Alice71')==b'PROFILE SAVED'
        assert cmd(a,'PROFILESET ADDRESS private-address-71')==b'PROFILE SAVED'
        assert post(a,'First','Body71')==1
        assert post(a,'Second','Body72')==2
        assert post(a,'Withdrawn','retired71')==3
        assert cmd(a,'WITHDRAW 3 CONFIRM')==b'WITHDRAW SAVED 3'
        assert cmd(a,'HIDE 1')==b'HIDDEN SAVED 1'
        assert cmd(a,'RESTORE 1')==b'RESTORED 1'
        read(b,1)
    finally:a.close();b.close();s.close()
    with sqlite3.connect(db) as d:
        assert d.execute('SELECT version FROM storage_meta').fetchone()==(3,)
        assert d.execute('SELECT count(*) FROM profile_events').fetchone()[0]>=6
        assert d.execute('SELECT count(*) FROM content_events').fetchone()[0]>=1
        assert d.execute('SELECT count(*) FROM moderation_events').fetchone()[0]==2
        assert d.execute("SELECT text FROM profile_events WHERE event_type=74 AND text='private-address-71'").fetchone()
        assert d.execute('SELECT count(*) FROM blocks WHERE stream IN (SELECT stream FROM record_streams)').fetchone()==(0,)
    export=base/'export'
    # Export must be outside the instance root; this fixture uses a dedicated child root.
    external=base.parent/(base.name+'-export')
    try:
        subprocess.run([sys.executable,ROOT/'scripts/maintenance.py','db-export','--root',base,'--database',db,'--board','notes',external],check=True,capture_output=True)
        assert (external/'members.profile').read_bytes().startswith(b'LNPROF01')
        s=Server(binary,base/'notes',members=base/'members',menu=True,telnet=True,language='en',options=options)
        c=Telnet(s.port)
        try:
            c.until();c.cmd('LOGIN bob',b'> ');c.cmd(PASSWORD)
            assert b'Body71' in c.cmd('READ 1')
            text=c.cmd('s');assert b'Alice71' in text and b'private-address-71' not in text,text
            assert b'Body71' in c.cmd('L')
            assert b'Body72' in c.cmd('+1')
            assert b'Alice71' in c.cmd('S')
        finally:c.close();s.close()
        s=Server(binary,external/'notes',members=external/'members');c=Client(s.port)
        try:
            auth(c,'alice');assert any(b'private-address-71' in line for line in rows(c,'PROFILE',b'END PROFILE'))
        finally:c.close();s.close()
    finally:
        import shutil
        if external.exists():shutil.rmtree(external)
    print('PASS structured profile/history rows, logical export/replay, reader S privacy and unchanged read position')

if __name__=='__main__':
    binary=str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='stage71-') as directory:
        base=Path(directory);receipt_checks(base);spool_checks(base)
        instance=base/'instance';instance.mkdir();database_reader(binary,instance)
