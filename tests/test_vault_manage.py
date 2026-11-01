import pathlib,sys,tempfile,subprocess,hashlib,struct,datetime
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_notes import page
from test_vault_catalog import run as old_catalog
from test_telnet import Client as Telnet

def run(binary,ctl):
    binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
    def cli(*args,okay=True):
        r=subprocess.run([ctl,*map(str,args)],capture_output=True,text=True,encoding='utf-8',timeout=20)
        assert (r.returncode==0)==okay,(args,r.stdout,r.stderr)
        return r.stdout
    def cmd(c,text,prefix):
        c.send((text+'\n').encode());r=c.line();assert r.startswith(prefix),(text,r);return r
    with tempfile.TemporaryDirectory(prefix='vmanage-') as d:
        p=pathlib.Path(d);vault=p/'vault';source=p/'source';source.write_bytes(b'original')
        cli('init',vault)
        for i in range(12):cli('import',vault,source,f'file-{i+1}.bin','old description')
        journal=vault/'index.dat';raw=bytearray(journal.read_bytes())
        for i in range(12):
            pos=8+640*i;stamp=int(datetime.datetime(2020,1,i+1,tzinfo=datetime.timezone.utc).timestamp())
            struct.pack_into('<Q',raw,pos+12,stamp);raw[pos+608:pos+640]=hashlib.sha256(raw[pos:pos+608]).digest()
        journal.write_bytes(raw)
        data=p/'notes';users=p/'members';admins=p/'admins';admins.write_text('1\n')
        s=Server(binary,data,members=users);a,b=Client(s.port),Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True)
        finally:a.close();b.close();s.close()
        args=dict(members=users,admins=admins,vault=vault)
        s=Server(binary,data,**args);a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob')
            for command in ['FILEDESC 1 changed','FILERESTORE 1 CONFIRM']:cmd(b,command,b'ERR vault denied')
            cmd(a,'FILEDESC 1 '+('説'*80),b'FILE EDITED 1')
            before=journal.read_bytes();cmd(a,'FILEDESC 1 '+('説'*80),b'FILE EDITED 1');assert before==journal.read_bytes()
            cmd(a,'FILEDESC 1 '+('x'*241),b'ERR vault syntax')
            cmd(a,'FILEHIDE 1 CONFIRM',b'FILE HIDDEN 1');assert not page(b,'FILESEARCH file-1.bin')[1]
            cmd(a,'FILEDESC 1 restored text',b'FILE EDITED 1')
            blob=vault/'00000001.bin';blob.write_bytes(b'corrupt!')
            cmd(a,'FILERESTORE 1 CONFIRM',b'ERR vault storage');assert not page(b,'FILESEARCH file-1.bin')[1]
            blob.write_bytes(b'original');cmd(a,'FILERESTORE 1 CONFIRM',b'FILE RESTORED 1')
            before=journal.read_bytes();cmd(a,'FILERESTORE 1 CONFIRM',b'FILE RESTORED 1');assert before==journal.read_bytes()
            assert len(page(b,'FILESEARCH restored text')[1])==1
            rows=page(b,'FILENEWS 2020-01-05')[1];assert len(rows)==8 and rows[0].startswith(b'FILEITEM 5 ')
            _,rows,next_=page(b,'FILENEWS 2020-01-01');assert len(rows)==8 and next_==9
            assert len(page(b,f'FILENEWSFROM {next_} 2020-01-01')[1])==4
            assert not page(b,'FILENEWS 9999-12-31')[1]
            assert len(page(b,'FILENEWS 2000-02-29')[1])==8
            for date in ['2020-02-30','2021-02-29','1900-02-29','2020-13-01','2020-00-01','2020-1-01','2020-01-01junk']:
                cmd(b,'FILENEWS '+date,b'ERR vault syntax')
            events=[];cursor=0
            while True:
                _,rows,cursor=page(a,'FILEAUDIT'+(f' {cursor}' if cursor else ''));events+=rows
                if not cursor:break
            assert any(b' E\tlegacy-member:1' in r for r in events)
            assert any(b' S\tlegacy-member:1' in r for r in events)
        finally:a.close();b.close();s.close()
        assert 'restored text' in cli('info',vault,1)
        cli('describe',vault,1,'');assert 'restored text' not in cli('info',vault,1)
        cli('hide',vault,1);cli('restore',vault,1)
        s=Server(binary,data,**args,menu=True,telnet=True,language='en');c=Telnet(s.port)
        try:
            c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD)
            assert b'New files since' in c.cmd('FILENEWS 2020-01-01')
            c.cmd('1');assert b'file-12.bin' in c.cmd('+')
            assert b'Updated description' in c.cmd('FILEDESC 1 English')
            c.cmd('LANG ja');assert '説明を更新'.encode() in c.cmd('FILEDESC 1 日本語')
            c.cmd('FILEHIDE 1 CONFIRM');assert '再公開'.encode() in c.cmd('FILERESTORE 1 CONFIRM')
            c.cmd('VAULT');assert '新着ファイル'.encode() in c.cmd('N 2020-01-05')
        finally:c.close();s.close()
        raw=journal.read_bytes();journal.write_bytes(raw[:-1]);cli('list',vault,okay=False);assert journal.read_bytes()==raw[:-1]
    print('PASS: admin metadata editing, empty/maximum text, no-op persistence, hidden-file editing, integrity-checked restore, audit, UTC date boundaries, pagination, JP/EN Telnet and corrupted journal rejection')
if __name__=='__main__':run(*sys.argv[1:])
