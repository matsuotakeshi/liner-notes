"""Independent vault CLI, BBS adapter, member authorization and real downloads."""
import base64
import hashlib
import importlib.util
import os
import pathlib
import subprocess
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_moderation import expect
from test_notes import page
from test_ui import Menu

ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('vault_download',ROOT/'scripts/vault-download.py')
downloader=importlib.util.module_from_spec(spec);spec.loader.exec_module(downloader)


def run(binary,ctl):
    binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
    def cli(*args,ok=True):
        result=subprocess.run([ctl]+list(map(str,args)),capture_output=True,text=True,encoding='utf-8',timeout=15)
        if ok:assert result.returncode==0,(args,result.stdout,result.stderr)
        else:assert result.returncode!=0,(args,result.stdout)
        return result
    with tempfile.TemporaryDirectory(prefix='vault-') as d:
        p=pathlib.Path(d);vault=p/'separate vault';data=p/'bbs';users=p/'users';admins=p/'admins'
        cli('init',vault);cli('init',vault)
        source=p/'source.bin';payload=bytes(range(256))*17+b'\x00\xffend';source.write_bytes(payload)
        first=cli('import',vault,source,'日本語.bin','binary test').stdout
        assert first.startswith('1 ') and hashlib.sha256(payload).hexdigest() in first
        original=source.read_bytes()
        for name in ['../escape','a/b','a\\b','a:b','.','..','x\x1by']:
            cli('import',vault,source,name,'bad',ok=False)
        assert source.read_bytes()==original
        cli('export',vault,1,p/'export.bin');assert (p/'export.bin').read_bytes()==payload
        cli('export',vault,1,p/'export.bin',ok=False);assert (p/'export.bin').read_bytes()==payload
        empty=p/'empty';empty.write_bytes(b'');cli('import',vault,empty,'empty','zero bytes')
        s=Server(binary,data,members=users);a,b=Client(s.port),Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True)
        finally:a.close();b.close();s.close()
        admins.write_text('1\n')
        args=dict(members=users,admins=admins,vault=vault)
        s=Server(binary,data,**args);a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob')
            expect(g,'FILES','ERR login required');expect(g,'FILEGET 1 0','ERR login required')
            expect(b,'FILEHIDE 1 CONFIRM','ERR vault denied')
            expect(a,'FILEHIDE 1','ERR vault syntax')
            header,rows,cursor=page(b,'FILES');assert header==b'FILES' and len(rows)==2 and cursor==0
            b.send(b'FILEGET 1 0\n');parts=b.line().split();assert parts[:4]==[b'FILEDATA',b'1',b'0',b'768']
            assert base64.b64decode(parts[4])==payload[:768]
            expect(b,'FILEGET 1 999999','ERR vault syntax')
            # Local publishing does not require restarting the BBS or opening its account files.
            for i in range(3,11):cli('import',vault,empty,f'file-{i}','more')
            _,rows,cursor=page(b,'FILES');assert len(rows)==8 and cursor==9
            assert len(page(b,'FILES 9')[1])==2
            count,digest=downloader.download('127.0.0.1',s.port,'bob',PASSWORD,1,p/'download.bin')
            assert count==len(payload) and digest==hashlib.sha256(payload).hexdigest() and (p/'download.bin').read_bytes()==payload
            downloader.download('127.0.0.1',s.port,'bob',PASSWORD,2,p/'zero.bin');assert (p/'zero.bin').stat().st_size==0
            try:downloader.download('127.0.0.1',s.port,'bob',PASSWORD,1,p/'download.bin')
            except ValueError:pass
            else:raise AssertionError('overwrote destination')
            if os.name=='posix':
                import fcntl
                with (vault/'index.dat').open('r+b') as f:
                    fcntl.lockf(f,fcntl.LOCK_EX|fcntl.LOCK_NB)
                    expect(a,'FILES','ERR vault busy');expect(a,'PING','PONG')
                blob=vault/'00000001.bin';blob.rename(vault/'saved.bin');blob.symlink_to(source)
                try:expect(b,'FILEGET 1 0','ERR vault storage')
                finally:blob.unlink();(vault/'saved.bin').rename(blob)
            blob=vault/'00000001.bin';blob.write_bytes(bytes([payload[0]^1])+payload[1:])
            try:
                try:downloader.download('127.0.0.1',s.port,'bob',PASSWORD,1,p/'same-size-bad.bin')
                except ValueError as error:assert 'SHA-256' in str(error)
                else:raise AssertionError('same-size corruption accepted')
                assert not (p/'same-size-bad.bin').exists()
            finally:blob.write_bytes(payload)
            expect(a,'FILEHIDE 1 CONFIRM','FILE HIDDEN 1')
            expect(b,'FILE 1','ERR no file');expect(b,'FILEGET 1 768','ERR no file')
            assert (vault/'00000001.bin').read_bytes()==payload
            expect(a,'FILEHIDE 1 CONFIRM','FILE HIDDEN 1')
        finally:a.close();b.close();g.close();s.close()
        snapshot={f:f.read_bytes() for f in p.iterdir() if f.is_file() and '-index.sqlite' not in f.name and (f.name.startswith('bbs') or f.name=='users')}
        s=Server(binary,data,**args,menu=True,telnet=True,language='en')
        from test_telnet import Client as Telnet
        a=Telnet(s.port)
        try:
            a.until();a.cmd('LOGIN alice',b'> ');a.cmd(PASSWORD)
            assert b'File vault' in a.cmd('VAULT') and b'file-10' in a.cmd('+')
            assert b'File #2' in a.cmd('FILE 2') and b'zero bytes' in a.cmd('FILE 2')
            assert b'ZUPLOAD' in a.cmd('HELP FILES')
            a.cmd('LANG ja');assert 'ファイル保管庫'.encode() in a.cmd('VAULT')
            assert 'ファイル #2'.encode() in a.cmd('FILE 2')
            assert '専用クライアント'.encode() in a.cmd('FILEGET 2 0')
            downloader.download('127.0.0.1',s.port,'bob',PASSWORD,2,p/'telnet-zero.bin')
        finally:a.close();s.close()
        # Language changes are saved on disconnect; content stores stay untouched.
        assert all(f.read_bytes()==v for f,v in snapshot.items() if f.name!='users')
        assert (p/'users').read_bytes().startswith(snapshot[p/'users'])
        print('PASS: standalone import/export, binary SHA-256 download, empty files, live publication, member/admin permissions, hide/restart, bilingual menus and Telnet')

        s=Server(binary,data,**args);a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob');expect(a,'SUSPEND 2','SUSPENDED 2')
            try:downloader.download('127.0.0.1',s.port,'bob',PASSWORD,2,p/'blocked.bin')
            except ValueError:pass
            else:raise AssertionError('suspended account downloaded')
            expect(a,'RESUME 2','RESUMED 2')
        finally:a.close();b.close();s.close()
        # Invalid journal data never gets silently repaired, and remains isolated from BBS stores.
        journal=vault/'index.dat';saved=journal.read_bytes()
        journal.write_bytes(saved[:-1]);cli('list',vault,ok=False);assert journal.read_bytes()==saved[:-1]
        result=subprocess.run([binary,'--vault-dir',str(vault),'0',str(data),str(users)],capture_output=True,timeout=5)
        assert result.returncode and b'vault:' in result.stderr
        journal.write_bytes(saved)
        blob=vault/'00000002.bin';blob.write_bytes(b'corrupt')
        s=Server(binary,data,**args);a=Client(s.port)
        try:
            auth(a,'alice');expect(a,'FILEGET 2 0','ERR vault storage');expect(a,'PING','PONG')
            try:downloader.download('127.0.0.1',s.port,'alice',PASSWORD,2,p/'bad.bin')
            except ValueError:pass
            else:raise AssertionError('corrupt download accepted')
            assert not (p/'bad.bin').exists()
        finally:a.close();s.close()
        cli('import',vault,source,'after-corruption','new record')
        print('PASS: suspension, lock contention, no path traversal, exclusive outputs, corrupt data refusal and BBS isolation')


if __name__=='__main__':run(sys.argv[1],sys.argv[2])
