"""Vault-owned categories/search/audit, legacy records, access and durability."""
import hashlib
import pathlib
import struct
import subprocess
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth, PASSWORD
from test_moderation import expect
from test_notes import page
from test_telnet import Client as Telnet


def run(binary, ctl):
    binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
    def cli(*args,ok=True):
        r=subprocess.run([ctl,*map(str,args)],capture_output=True,text=True,encoding='utf-8',timeout=15)
        assert (r.returncode==0)==ok,(args,r.stdout,r.stderr)
        return r.stdout
    with tempfile.TemporaryDirectory(prefix='vault-catalog-') as d:
        p=pathlib.Path(d);vault=p/'vault';source=p/'source';source.write_bytes(b'original')
        cli('init',vault)
        for i in range(1,13):cli('import',vault,source,f'doc-{i}.txt','日本語の資料 Alpha')
        journal=vault/'index.dat'
        # A genuine stage33-style D record has no actor/time fields of its own.
        legacy=bytearray(journal.read_bytes()[8:648]);legacy[0]=ord('D')
        legacy[608:]=hashlib.sha256(legacy[:608]).digest()
        with journal.open('ab') as f:f.write(legacy)
        prefix=journal.read_bytes()
        assert 'actor=unknown' in cli('audit',vault)
        for i in range(2,12):cli('category',vault,i,'資料 Docs')
        cli('category',vault,12,'Tools')
        assert journal.read_bytes().startswith(prefix)
        assert '10 資料 Docs' in cli('categories',vault)
        assert len(cli('search',vault,'aLpHa').splitlines())==11
        assert len(cli('in',vault,'資料 Docs').splitlines())==10
        assert 'CATEGORY 資料 Docs' in cli('info',vault,2)
        for invalid in ['x\x1by','x'*49]:cli('category',vault,2,invalid,ok=False)
        saved=journal.read_bytes();cli('category',vault,2,'資料 Docs');assert journal.read_bytes()==saved
        data=p/'bbs';users=p/'users';admins=p/'admins'
        s=Server(binary,data,members=users);a,b=Client(s.port),Client(s.port)
        try:auth(a,'alice',True);auth(b,'bob',True)
        finally:a.close();b.close();s.close()
        admins.write_text('1\n');args=dict(members=users,admins=admins,vault=vault)
        stores={f:f.read_bytes() for f in p.iterdir() if f.is_file() and '-index.sqlite' not in f.name and (f.name.startswith('bbs') or f.name=='users')}
        s=Server(binary,data,**args);a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob')
            for cmd in ['FILEAUDIT','FILECATEGORIES','FILESEARCH Alpha','FILESIN Tools']:expect(g,cmd,'ERR login required')
            expect(b,'FILEAUDIT','ERR vault denied');expect(b,'FILECATEGORY 2 Stolen','ERR vault denied')
            header,rows,next_=page(b,'FILESEARCH aLpHa');assert header==b'FILESEARCH aLpHa' and len(rows)==8 and next_==10
            assert len(page(b,f'FILESEARCHFROM {next_} aLpHa')[1])==3
            assert len(page(b,'FILESIN 資料 Docs')[1])==8
            assert len(page(b,'FILESINFROM 10 資料 Docs')[1])==2
            assert page(b,'FILESIN -')[1]==[]
            assert page(b,'FILESEARCH doc-1.txt')[1]==[]  # legacy hidden file
            cats=page(b,'FILECATEGORIES')[1];assert b'VCATEGORY 10 '+ '資料 Docs'.encode() in cats
            for cmd in ['FILESEARCHFROM 0 a','FILECATEGORY 2','FILEAUDIT 2147483647','FILECATEGORIES 2147483647','FILESIN '+('x'*49)]:expect(a,cmd,'ERR vault syntax')
            expect(a,'FILECATEGORY 2 Tools','FILE CATEGORY 2')
            audit=[];cursor=0
            while True:
                _,rows,cursor=page(a,'FILEAUDIT'+(f' {cursor}' if cursor else ''));audit+=rows
                if not cursor:break
            assert len(audit)==25 and b'\tlegacy-member:1\t'+ '資料 Docs'.encode()+b'\tTools' in audit[-1]
            expect(a,'FILEHIDE 2 CONFIRM','FILE HIDDEN 2')
            assert not page(b,'FILESEARCH doc-2.txt')[1]
            assert page(b,'FILESIN Tools')[1]==[b'FILEITEM 12 8 doc-12.txt']
            assert b'VCATEGORY 1 Tools' in page(b,'FILECATEGORIES')[1]
            saved=journal.read_bytes();expect(a,'FILEHIDE 2 CONFIRM','FILE HIDDEN 2');assert journal.read_bytes()==saved
            expect(a,'FILECATEGORY 2 Hidden','ERR no file')
            cli('category',vault,12,'CLI change')
            assert len(page(b,'FILESIN CLI change')[1])==1
            expect(a,'FILECATEGORY 12 -','FILE CATEGORY 12');assert len(page(b,'FILESIN -')[1])==1
        finally:a.close();b.close();g.close();s.close()
        assert all(f.read_bytes()==content for f,content in stores.items() if f.name!='users')
        assert (p/'users').read_bytes().startswith(stores[p/'users'])
        assert 'actor=legacy-member:1' in cli('audit',vault) and 'actor=local-operator' in cli('audit',vault)
        s=Server(binary,data,**args,menu=True,telnet=True,language='en');a,b=Telnet(s.port),Telnet(s.port)
        try:
            for c,user in [(a,'alice'),(b,'bob')]:c.until();c.cmd('LOGIN '+user,b'> ');c.cmd(PASSWORD)
            a.cmd('PAGER OFF');b.cmd('PAGER OFF')
            assert b'Vault search' in a.cmd('FILESEARCH Alpha')
            assert b'Category: '+ '資料 Docs'.encode() in a.cmd('3')
            assert b'doc-12.txt' in a.cmd('+')  # details preserve search cursor
            assert b'Category:' in a.cmd('FILESIN 資料 Docs')
            assert b'doc-11.txt' in a.cmd('+')
            assert b'Vault categories' in a.cmd('FILECATEGORIES')
            assert b'FILESIN category' in a.cmd('2')  # no accidental article navigation
            assert b'permission' in b.cmd('FILEAUDIT')
            assert b'Vault audit' in a.cmd('FILEAUDIT')
            assert b'#5 File' in a.cmd('+')
            assert b'FILE id' in a.cmd('2')
            a.cmd('LANG ja');assert '保管庫の操作履歴'.encode() in a.cmd('FILEAUDIT')
            assert 'カテゴリ'.encode() in a.cmd('FILECATEGORIES')
            assert '保管庫検索'.encode() in a.cmd('FILESEARCH 日本語')
            a.cmd('LANG en');a.cmd('PAGER SIZE 80 5');a.cmd('PAGER ON')
            response=a.cmd('FILEAUDIT',b'[Enter/Q/OFF]> ');assert b'[Enter/Q/OFF]> ' in response
            a.cmd('q');a.cmd('PAGER OFF')
        finally:a.close();b.close();s.close()
        assert all(f.read_bytes()==content for f,content in stores.items() if f.name!='users')
        assert (p/'users').read_bytes().startswith(stores[p/'users'])
        print('PASS: independent category/search/audit, permissions, legacy unknown actor, no-op records, live changes, durable actors, bilingual Telnet, pagination and BBS store isolation')
        saved=journal.read_bytes()
        # A partial change cannot turn into an unlogged success; reject unchanged.
        journal.write_bytes(saved[:-1]);cli('list',vault,ok=False);assert journal.read_bytes()==saved[:-1]
        # Even a rechecksummed control-character actor is rejected.
        bad=bytearray(saved);last=len(bad)-640;bad[last+24]=27;bad[last+608:]=hashlib.sha256(bad[last:last+608]).digest()
        journal.write_bytes(bad);cli('audit',vault,ok=False);assert journal.read_bytes()==bad
        journal.write_bytes(saved)
        # Bounded journal: generate valid alternating category changes up to cap.
        record=bytearray(saved[-640:]);assert record[0]==ord('C')
        count=(len(saved)-8)//640
        with journal.open('ab') as f:
            for i in range(count,10000):
                category=b'a' if (i-count)%2==0 else b'b'
                record[20:24]=struct.pack('<I',1);record[104:153]=category+b'\0'*48
                record[608:]=hashlib.sha256(record[:608]).digest();f.write(record)
        full=journal.read_bytes();cli('category',vault,12,'limit',ok=False);cli('hide',vault,12,ok=False)
        assert journal.read_bytes()==full and 'doc-12.txt' in cli('list',vault)
        print('PASS: truncated/rechecksummed journal refusal and event-limit writes fail without losing readable files')


if __name__=='__main__':run(sys.argv[1],sys.argv[2])
