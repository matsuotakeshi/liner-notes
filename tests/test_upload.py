"""Durable staged uploads through the independent CLI and live BBS reader."""
import hashlib
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth
from test_notes import page
from test_vault import downloader, PASSWORD
from test_telnet import Client as Telnet


def run(binary,ctl):
    binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
    def cli(*args,ok=True,preexec_fn=None):
        result=subprocess.run([ctl,*map(str,args)],capture_output=True,text=True,encoding='utf-8',timeout=20,preexec_fn=preexec_fn)
        assert (result.returncode==0)==ok,(args,result.stdout,result.stderr)
        return result.stdout
    def reserve(vault,payload,name='staged.bin',hash_=None):
        return int(cli('upload-start',vault,len(payload),hash_ or hashlib.sha256(payload).hexdigest(),name,'staged description').split()[1])
    with tempfile.TemporaryDirectory(prefix='vault-upload-') as d:
        p=pathlib.Path(d);vault=p/'vault';cli('init',vault)
        data=p/'bbs';users=p/'users';admins=p/'admins';admins.write_text('1\n')
        s=Server(binary,data,members=users);c=Client(s.port)
        try:auth(c,'alice',True)
        finally:c.close();s.close()
        stores={f:f.read_bytes() for f in p.iterdir() if f.is_file() and '-index.sqlite' not in f.name and (f.name.startswith('bbs') or f.name=='users')}
        args=dict(members=users,admins=admins,vault=vault)
        s=Server(binary,data,**args);c=Client(s.port)
        payload=bytes(range(256))*7+b'\xff\0end';chunk=p/'chunk'
        try:
            auth(c,'alice');ident=reserve(vault,payload,'日本語.bin')
            assert page(c,'FILES')[1]==[] and not page(c,'FILESEARCH 日本語')[1]
            journal=vault/'index.dat';reservation=journal.read_bytes()
            cli('upload-finish',vault,ident,ok=False);assert journal.read_bytes()==reservation
            chunk.write_bytes(payload[:700]);assert 'RECEIVED 700' in cli('upload-write',vault,ident,0,chunk)
            assert 'ACTIVE 700' in cli('upload-status',vault,ident)
            assert 'RECEIVED 700' in cli('upload-write',vault,ident,0,chunk)
            chunk.write_bytes(b'x');cli('upload-write',vault,ident,0,chunk,ok=False)
            cli('upload-write',vault,ident,701,chunk,ok=False)
            chunk.write_bytes(b'x'*769);cli('upload-write',vault,ident,700,chunk,ok=False)
            for offset in range(700,len(payload),768):
                chunk.write_bytes(payload[offset:offset+768]);cli('upload-write',vault,ident,offset,chunk)
            assert journal.read_bytes()==reservation  # chunks do not exhaust history
            assert page(c,'FILES')[1]==[]
            # Simulate restart after link+unlink, before publication record.
            part=vault/f'{ident:08d}.part';blob=vault/f'{ident:08d}.bin'
            os.link(part,blob);part.unlink()
            assert page(c,'FILES')[1]==[]
            cli('upload-finish',vault,ident);committed=journal.read_bytes()
            cli('upload-finish',vault,ident);assert journal.read_bytes()==committed
            assert 'COMMITTED' in cli('upload-status',vault,ident)
            assert len(page(c,'FILES')[1])==1
            downloader.download('127.0.0.1',s.port,'alice',PASSWORD,ident,p/'download')
            assert (p/'download').read_bytes()==payload
            cli('upload-cancel',vault,ident,ok=False);assert blob.read_bytes()==payload
            bad=reserve(vault,b'abc',hash_='0'*64);chunk.write_bytes(b'abc');cli('upload-write',vault,bad,0,chunk)
            cli('upload-finish',vault,bad,ok=False);assert not (vault/f'{bad:08d}.bin').exists()
            cli('upload-cancel',vault,bad);cancelled=journal.read_bytes()
            cli('upload-cancel',vault,bad);assert journal.read_bytes()==cancelled
            assert not (vault/f'{bad:08d}.part').exists() and 'CANCELLED' in cli('upload-status',vault,bad)
            zero=reserve(vault,b'','empty');cli('upload-finish',vault,zero)
            assert (vault/f'{zero:08d}.bin').stat().st_size==0
        finally:c.close();s.close()
        assert all(f.read_bytes()==content for f,content in stores.items())
        s=Server(binary,data,**args,menu=True,telnet=True,language='en');c=Telnet(s.port)
        try:
            c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD);c.cmd('PAGER OFF')
            assert b'Upload reserved' in c.cmd('FILEAUDIT')
            assert b'Upload published' in c.cmd('+')
            c.cmd('LANG ja');assert 'アップロード予約'.encode() in c.cmd('FILEAUDIT')
        finally:c.close();s.close()
        print('PASS: staged files remain private, retry/resume, no overwrite, hash rejection, empty files, durable publication/cancellation, bilingual audit and live BBS download')

        # Quota includes reservations AND orphan numbered files; cancellation releases reservation.
        quota=p/'quota';cli('init',quota);(quota/'limits.conf').write_text('max_bytes=268435456\n')
        with (quota/'00000050.bin').open('wb') as f:f.truncate(240*1024*1024)
        q=int(cli('upload-start',quota,16*1024*1024,'0'*64,'reserved','').split()[1]);assert q==51
        cli('upload-start',quota,1,'0'*64,'over','',ok=False)
        chunk.write_bytes(b'x');cli('import',quota,chunk,'over','',ok=False)
        cli('upload-cancel',quota,q);cli('import',quota,chunk,'fits','')
        assert (quota/'00000050.bin').stat().st_size==240*1024*1024

        # Both hard links can remain after interruption; finish removes only staging.
        two=reserve(vault,b'abc');chunk.write_bytes(b'abc');cli('upload-write',vault,two,0,chunk)
        os.link(vault/f'{two:08d}.part',vault/f'{two:08d}.bin');cli('upload-finish',vault,two)
        assert not (vault/f'{two:08d}.part').exists()
        if os.name=='posix':
            import fault_io as resource
            import signal
            symlink=reserve(vault,b'x');target=p/'do-not-change';target.write_bytes(b'x')
            staging=vault/f'{symlink:08d}.part';staging.symlink_to(target)
            chunk.write_bytes(b'x');cli('upload-write',vault,symlink,0,chunk,ok=False)
            cli('upload-finish',vault,symlink,ok=False);cli('upload-cancel',vault,symlink,ok=False)
            assert target.read_bytes()==b'x';staging.unlink();cli('upload-cancel',vault,symlink)
            # OS-level partial journal write: do not acknowledge or silently repair.
            short=reserve(vault,b'');journal=vault/'index.dat';before=journal.read_bytes()
            def limit():
                signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE,(len(before)+32,len(before)+32))
            cli('upload-finish',vault,short,ok=False,preexec_fn=limit)
            assert journal.stat().st_size==len(before)+32
            damaged=journal.read_bytes();cli('list',vault,ok=False);assert journal.read_bytes()==damaged
            journal.write_bytes(before)  # Restore ONLY this synthetic test fixture.
            cli('upload-finish',vault,short)
        print('PASS: reservation quota shared with import, cancel release and interrupted link recovery')
        if os.name=='posix':print('PASS: no-follow staging and POSIX partial-journal failure')

        # Reserve the completion record even when other metadata fills the journal.
        bounded=p/'bounded';cli('init',bounded);chunk.write_bytes(b'x');cli('import',bounded,chunk,'file','')
        pending=reserve(bounded,b'');cli('category',bounded,1,'a');cli('category',bounded,1,'b')
        journal=bounded/'index.dat';saved=journal.read_bytes();record=bytearray(saved[-640:])
        with journal.open('ab') as f:
            for i in range(4,9999):
                record[20:24]=struct.pack('<I',1);record[104:153]=(b'a' if i%2==0 else b'b')+b'\0'*48
                record[608:]=hashlib.sha256(record[:608]).digest();f.write(record)
        before=journal.read_bytes()
        cli('category',bounded,1,'blocked',ok=False);cli('import',bounded,chunk,'blocked','',ok=False)
        cli('upload-start',bounded,0,hashlib.sha256(b'').hexdigest(),'blocked','',ok=False)
        assert journal.read_bytes()==before
        cli('upload-finish',bounded,pending);assert journal.stat().st_size==8+10000*640
        cli('upload-finish',bounded,pending)
        journal.write_bytes(before);(bounded/f'{pending:08d}.bin').unlink()  # alternate crash-free cancellation fixture
        cli('upload-cancel',bounded,pending);assert journal.stat().st_size==8+10000*640
        print('PASS: active uploads retain a final journal slot for commit or cancellation at the history limit')


if __name__=='__main__':run(sys.argv[1],sys.argv[2])
