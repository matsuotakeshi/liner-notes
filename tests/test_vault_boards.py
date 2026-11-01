"""File board hierarchy, live ACL enforcement and private upload publication."""
import base64,hashlib,pathlib,subprocess,sys,tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_moderation import expect
from test_notes import page
from test_ui import Menu
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from transfer_connectors import LineConnector,BinaryConnector,RemoteError

def run(binary,ctl):
    binary,ctl=[str(pathlib.Path(x).resolve()) for x in (binary,ctl)]
    def cli(*args,ok=True):
        r=subprocess.run([ctl,*map(str,args)],capture_output=True,text=True,encoding='utf-8',timeout=15)
        assert (r.returncode==0)==ok,(args,r.stdout,r.stderr)
        return r.stdout
    def denied(fn):
        try:fn()
        except RemoteError as e:assert str(e) in ('ERR vault denied','ERR no file'),str(e)
        else:raise AssertionError('Unauthorized operation succeeded')
    with tempfile.TemporaryDirectory(prefix='vault-boards-') as d:
        p=pathlib.Path(d);v=p/'vault';data=p/'notes';members=p/'members';admins=p/'admins';src=p/'src';src.write_bytes(b'secret');digest=hashlib.sha256(b'secret').hexdigest()
        cli('init',v);cli('import',v,src,'legacy','old');cli('category',v,1,'legacy')
        s=Server(binary,data,members=members);a,b=Client(s.port),Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True)
        finally:a.close();b.close();s.close()
        admins.write_text('1\n')
        cli('board-set',v,'missing/child','all','all',ok=False)
        cli('board-set',v,'-','member','member')
        cli('board-set',v,'private','l3+g2','none')
        cli('board-set',v,'private/docs','member','l4+g2')
        cli('board-set',v,'private2','member','member')
        cli('import-in',v,'private/docs',src,'secret','confidential')
        for path,read,write in [('bad//path','all','all'),('../x','all','all'),('x/','all','all'),('x','l6','all'),('x','g101','all'),('x','g2+g2','all'),('x','all','l3+'),('x|y','all','all')]:
            cli('board-set',v,path,read,write,ok=False)
        before=(v/'index.dat').read_bytes();cli('board-set',v,'private','l3+g2','none');assert (v/'index.dat').read_bytes()==before
        def server(**kw):return Server(binary,data,members=members,admins=admins,vault=v,**kw)
        s=server();a,b=Client(s.port),Client(s.port);c=LineConnector('127.0.0.1',s.port,'bob',PASSWORD)
        try:
            auth(a,'alice');auth(b,'bob')
            assert not page(b,'FILES')[1]
            assert page(b,'FILEBOARDS')[1]==[b'VBOARD 1 -',b'VBOARD 1 private2']
            assert not page(b,'FILECATEGORIES')[1]
            for cmd in ['FILE 2','FILEMETA 2','FILEGET 2 0']:expect(b,cmd,'ERR no file')
            for cmd in ['FILESIN private','FILESEARCH secret','FILENEWS 2000-01-01']:assert not page(b,cmd)[1]
            c.upload_category='private/docs';denied(c.check_upload);denied(lambda:c.begin(6,digest,'new',''))
            bc=BinaryConnector('127.0.0.1',s.port,'bob',PASSWORD)
            try:
                denied(lambda:bc.info(2));denied(lambda:bc.read(2,0));bc.upload_category='private/docs';denied(lambda:bc.begin(6,digest,'binary',''))
            finally:bc.close()
            expect(a,'LEVEL 2 5','RIGHTS 2 LEVEL 5 GROUPS - STATUS active');b.close();c.close();b=Client(s.port);auth(b,'bob')
            assert not page(b,'FILESIN private')[1] # level 5 is not SYSOP
            expect(a,'GROUP 2 2','RIGHTS 2 LEVEL 5 GROUPS 2 STATUS active');b.close();b=Client(s.port);auth(b,'bob');c=LineConnector('127.0.0.1',s.port,'bob',PASSWORD)
            assert len(page(b,'FILESIN private')[1])==1
            c.upload_category='private';denied(c.check_upload)
            c.upload_category='private/docs';c.check_upload();ident=c.begin(6,digest,'new','private upload')
            assert len(page(b,'FILES')[1])==1 # reservation is not published
            c.write(ident,0,b'secret')
            cli('board-set',v,'private','none','none')
            denied(lambda:c.status(ident));denied(lambda:c.write(ident,0,b'secret'));denied(lambda:c.finish(ident));denied(lambda:c.info(2))
            cli('board-set',v,'private','l3+g2','none');assert c.finish(ident)==(6,digest)
            b.send(f'FILE {ident}\n'.encode());assert b.line().startswith(f'FILE {ident} '.encode())
            while b.line()!=b'END FILE':pass
            assert len(page(b,'FILESIN private')[1])==2
            expect(a,f'FILECATEGORY {ident} private2',f'FILE CATEGORY {ident}')
            assert len(page(b,'FILESIN private')[1])==1 # no prefix collision
            cli('board-set',v,'private2','none','member');denied(lambda:c.finish(ident))
            import importlib.util,json
            spec=importlib.util.spec_from_file_location('boards_upload',pathlib.Path(__file__).resolve().parents[1]/'scripts/vault-upload.py');upload=importlib.util.module_from_spec(spec);spec.loader.exec_module(upload)
            state=p/'upload.json'
            try:upload.upload('127.0.0.1',s.port,'bob',PASSWORD,src,state,category='private2')
            except RemoteError:pass
            else:raise AssertionError('denied category was published')
            assert json.loads(state.read_text())['category']=='private2'
            cli('board-set',v,'private2','member','member')
            restored,_,_=upload.upload('127.0.0.1',s.port,'bob',PASSWORD,src,state,resume=True)
            assert 'CATEGORY private2' in cli('info',v,restored)
            cli('board-set',v,'private2','none','member')
            cli('board-set',v,'legacy','member','member');assert len(page(b,'FILESEARCH legacy')[1])==1
            assert 'private/docs|member|l4+g2' in cli('boards',v)
        finally:a.close();b.close();c.close();s.close()
        for lang in ['en','ja']:
            s=server(menu=True,language=lang);m=Menu(s.port)
            try:
                m.until();m.cmd('LOGIN bob','> ');m.cmd(PASSWORD);m.cmd('PAGER OFF')
                text=m.cmd('FILEBOARDS');assert 'private/docs' in text and 'private2' not in text
                assert 'FILESIN' in m.cmd('2')
                m.cmd('VAULT');assert 'private/docs' in m.cmd('FILEBOARDS')
            finally:m.close();s.close()
        # Empty boards are paginated in both the wire API and the vault menu.
        for i in range(12):cli('board-set',v,f'empty{i:02}','member','member')
        s=server();b=Client(s.port)
        try:
            auth(b,'bob');cursor=0;rows=[]
            while True:
                _,part,cursor=page(b,'FILEBOARDS'+(f' {cursor}' if cursor else ''));rows+=part
                if not cursor:break
            assert len(rows)==16 and len(set(rows))==16,rows
        finally:b.close();s.close()
        s=server(menu=True,language='en');m=Menu(s.port)
        try:
            m.until();m.cmd('LOGIN bob','> ');m.cmd(PASSWORD);m.cmd('PAGER OFF');m.cmd('VAULT');text=m.cmd('FILEBOARDS');text+=m.cmd('+')
            assert all(f'empty{i:02}' in text for i in range(12))
        finally:m.close();s.close()
        # Journal policies and categorized records survive reopen; corrupted policy fails closed.
        assert 'private/docs' in cli('boards',v)
        saved=(v/'index.dat').read_bytes();damaged=bytearray(saved);damaged[8+2*640+104]^=1;(v/'index.dat').write_bytes(damaged)
        cli('boards',v,ok=False);(v/'index.dat').write_bytes(saved)
    print('PASS: hierarchical vault ACL, inherited reads, independent uploads, no ID bypass, live revocation, private publication, restart and bilingual menus')
if __name__=='__main__':run(*sys.argv[1:])
