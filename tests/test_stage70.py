"""Schema 2, persisted editor options, hourly fallback and transfer-spool safety."""
from pathlib import Path
import sys,tempfile,subprocess,sqlite3,json,hashlib,os,fcntl,time
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_mail import send
from test_stage58 import cmd,rows
from test_telnet import Client as Telnet
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from posix_locks import file_lock
import transfer_spool

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='stage70-') as tmp:
  p=Path(tmp);db=p/'liner.sqlite';opts=['--database-structured',str(db)]
  s=Server(binary,p/'notes',members=p/'members',options=opts);c=Client(s.port)
  try:
   auth(c,'alice',True)
   assert post(c,'first','small body')==1
   assert post(c,'large','A'*3000)==2
   assert send(c,'alice','private','B'*3000)==1
   maximum='\n'.join(['X'*4094]*64)
   assert post(c,'maximum',maximum)==3
   assert send(c,'alice','maximum mail',maximum)==2
   assert not cmd(c,'EDITOR LINE').startswith(b'ERR ')
   assert cmd(c,'PROTOCOL YMODEM')==b'PROTOCOL YMODEM'
   cmd(c,'BUFFER');cmd(c,'/M');cmd(c,'/CANCEL')
  finally:c.close();s.close()
  with sqlite3.connect(db) as d:
   assert d.execute('select version from storage_meta').fetchone()==(3,)
   assert d.execute('select title,length(body) from article_records where record_id<=2 order by offset').fetchall()==[('first',12),('large',3002)]
   assert d.execute("select login,length(salt),length(password_hash) from member_records where event_type=65").fetchone()==('alice',16,32)
   assert d.execute('select count(*) from blocks where stream in (select stream from record_streams)').fetchone()==(0,)
  # A clean restart and a crash both reconstruct bodies from authoritative rows.
  for crash in (True,False):
   s=Server(binary,p/'notes',members=p/'members',options=opts);c=Client(s.port)
   try:
    auth(c,'alice');assert read(c,2)[2]=='A'*3000+'\r\n'
    assert read(c,3)[2]==maximum.replace('\n','\r\n')+'\r\n'
    assert cmd(c,'PROTOCOL')==b'PROTOCOL YMODEM'
    assert cmd(c,'MAIL GET 1')==b'MAIL IMPORTED TO BUFFER'
   finally:c.close();s.close(crash=crash)
  atomic=p/'atomic.c';atomic_exe=p/'atomic'
  atomic.write_text('#include <stdio.h>\n#include "storage_sqlite.h"\n#include <assert.h>\n#include <fcntl.h>\nint main(int n,char **v){assert(n==3);assert(storage_init_structured(v[1]));int fd=storage_open(v[2],O_RDWR,0600);assert(fd>=0);assert(storage_seek(fd,0,SEEK_END)>8);char bad[16]={0};assert(storage_write(fd,bad,16)==16);assert(storage_sync(fd)==-1);storage_close(fd);storage_shutdown();return 0;}\n')
  subprocess.run(['cc','-std=c11','-D_XOPEN_SOURCE=700','-I'+str(ROOT/'src'),'-I/usr/local/include',atomic,ROOT/'src/storage_sqlite.c','-L/usr/local/lib','-lsqlite3','-lcrypto','-pthread','-o',atomic_exe],check=True)
  before=sqlite3.connect(db).execute('select length,revision from streams where name=?',('notes',)).fetchone()
  subprocess.run([atomic_exe,db,p/'notes'],check=True)
  assert sqlite3.connect(db).execute('select length,revision from streams where name=?',('notes',)).fetchone()==before
  backup=p.parent/(p.name+'-backup.tar.gz');export=p.parent/(p.name+'-export')
  try:
   tool=ROOT/'scripts/maintenance.py'
   subprocess.run([sys.executable,tool,'db-backup','--root',p,'--database',db,'--board','notes','--members','members',backup],check=True,capture_output=True)
   subprocess.run([sys.executable,tool,'restore',backup,export],check=True,capture_output=True)
   s=Server(binary,export/'notes',members=export/'members');c=Client(s.port)
   try:
    auth(c,'alice');assert read(c,2)[2]=='A'*3000+'\r\n'
    assert read(c,3)[2]==maximum.replace('\n','\r\n')+'\r\n';assert cmd(c,'PROTOCOL')==b'PROTOCOL YMODEM'
   finally:c.close();s.close()
  finally:
   import shutil
   if backup.exists():backup.unlink()
   if export.exists():shutil.rmtree(export)
  # New-only option must reject an existing schema-1 DB.
  v1=p/'old.sqlite';s=Server(binary,p/'oldnotes',members=p/'oldmembers',options=['--database',str(v1)]);s.close()
  result=subprocess.run([binary,'--database-structured',str(v1),'0',str(p/'oldnotes'),str(p/'oldmembers')],capture_output=True)
  assert result.returncode and sqlite3.connect(v1).execute('select version from storage_meta').fetchone()==(1,)
  # Every hour falls back to the preceding defined greeting, wrapping midnight.
  (p/'greeting-00.txt').write_bytes(b'\xef\xbb\xbf');(p/'greeting-18.txt').write_text('EVENING70\n');(p/'greeting-06.txt').write_text('MORNING70\n')
  code=p/'greet.c';exe=p/'greet'
  code.write_text('#include "menu.h"\n#include <assert.h>\n#include <string.h>\nint main(int n,char **v){assert(n==2);assert(login_text_load(v[1]));assert(strstr(hourly_greeting(0),"EVENING70"));assert(strstr(hourly_greeting(6),"MORNING70"));assert(strstr(hourly_greeting(17),"MORNING70"));assert(strstr(hourly_greeting(23),"EVENING70"));return 0;}\n')
  subprocess.run(['cc','-std=c11','-I'+str(ROOT/'src'),code,ROOT/'src/menu.c',ROOT/'src/messages.c','-o',exe],check=True)
  subprocess.run([exe,p],check=True)
  # Editor auxiliaries survive a disconnect, including the numbered prompt.
  s=Server(binary,p/'notes',members=p/'members',options=opts,menu=True,telnet=True,language='en');t=Telnet(s.port)
  try:
   t.until();t.cmd('LOGIN alice',b'> ');t.cmd(PASSWORD)
   t.cmd('I');settings=t.cmd('D');assert b'LINE NUMBERS ON' in settings and b'PROTOCOL YMODEM' in settings,settings
  finally:t.close();s.close()
 with tempfile.TemporaryDirectory(prefix='spool70-') as tmp:
  root=Path(tmp);identity=['127.0.0.1',2222,'alice','line-v1','files'];key=hashlib.sha256(json.dumps(identity).encode()).hexdigest();name='retained-'+key+'-fixture';work=root/name
  work.mkdir(mode=0o700);(work/'data').mkdir(mode=0o700);(work/'lock').touch(mode=0o600);meta=work/'transfer.json'
  meta.write_text(json.dumps({'identity':identity,'received':False}));meta.chmod(0o600)
  assert transfer_spool.manage(root,'list')[0]['phase']=='interrupted'
  assert not transfer_spool.manage(root,'restore',name)['applied'] and work.exists()
  transfer_spool.manage(root,'restore',name,True);name='pending-'+key;work=root/name
  lock=file_lock(work/'lock',create=False)
  try:assert transfer_spool.manage(root,'list')[0]['phase']=='active'
  finally:lock.close()
  meta=work/'transfer.json';meta.write_text(json.dumps({'identity':identity,'received':True}))
  try:transfer_spool.manage(root,'remove',name,True);raise AssertionError('publication removed')
  except ValueError:pass
  meta.write_text(json.dumps({'identity':identity,'received':False}));(work/'data'/'bad').symlink_to('/etc/passwd')
  try:transfer_spool.manage(root,'remove',name,True);raise AssertionError('symlink accepted')
  except ValueError:pass
  (work/'data'/'bad').unlink();transfer_spool.manage(root,'remove',name,True);assert not work.exists()
 print('PASS stage70 structured DB/restart/crash/logical backup, persistent editor/protocol, hourly fallback, spool preview/locks/publication/symlink safety')
if __name__=='__main__':run(sys.argv[1])
