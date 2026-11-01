"""Authoritative SQLite migration, restart, crash, exclusivity and rollback."""
from pathlib import Path
import hashlib,sqlite3,subprocess,sys,tempfile,os
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd,rows
from test_mail import send

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='database67-') as tmp:
  p=Path(tmp);board=p/'notes';members=p/'members';database=p/'liner.sqlite'
  s=Server(binary,board,members=members);c=Client(s.port)
  try:
   auth(c,'alice',True);post(c,'legacy','legacy article');send(c,'alice','legacy mail','private legacy mail')
   assert cmd(c,'PROFILESET MACHINE PC98')==b'PROFILE SAVED'
  finally:c.close();s.close()
  # The supported migration command creates a new copy and preserves the source.
  tool=Path(__file__).resolve().parents[1]/'scripts/maintenance.py'
  with tempfile.TemporaryDirectory(prefix='migrate67-') as migrated:
   destination=Path(migrated)/'copy'
   subprocess.run([sys.executable,str(tool),'db-migrate','--root',str(p),'--board','notes','--members','members',str(destination)],check=True,capture_output=True)
   migrated_server=Server(binary,destination/'notes',members=destination/'members',options=['--database',str(destination/'liner.sqlite')]);client=Client(migrated_server.port)
   try:auth(client,'alice');assert read(client,1)[2]=='legacy article\r\n'
   finally:client.close();migrated_server.close()
  originals={q:hashlib.sha256(q.read_bytes()).hexdigest() for q in p.iterdir() if q.is_file() and not q.name.endswith('.sqlite')}
  options=['--database',str(database)]
  s=Server(binary,board,members=members,options=options);c=Client(s.port)
  try:
   auth(c,'alice');assert read(c,1)[2]=='legacy article\r\n'
   assert post(c,'sqlite','SQLite authoritative article')==2
   assert cmd(c,'MAIL GET 1')==b'MAIL IMPORTED TO BUFFER';cmd(c,'MAIL EXIT')
   cmd(c,'PROFILESET MACHINE FreeBSD')
   assert b'MACHINE FreeBSD' in rows(c,'PROFILE',b'END PROFILE')
   second=subprocess.run([binary,*options,'0',str(board),str(members)],capture_output=True,timeout=5)
   assert second.returncode!=0 and b'database:' in second.stderr,second.stderr
  finally:s.close(crash=True);c.close()
  assert all(hashlib.sha256(q.read_bytes()).hexdigest()==value for q,value in originals.items())
  with sqlite3.connect(database) as db:
   assert db.execute('pragma integrity_check').fetchone()==('ok',)
   names={x[0] for x in db.execute('select name from streams')}
   assert {'notes','members','members.mail','members.profile'} <= names,names
  # Remove only fixtures from the live fixture directory; backups stay present.
  old=p/'legacy-backup';old.mkdir()
  for q in originals:q.rename(old/q.name)
  s=Server(binary,board,members=members,options=['--database',str(p)+'/./liner.sqlite']);c=Client(s.port)
  try:
   auth(c,'alice');assert read(c,2)[2]=='SQLite authoritative article\r\n'
   assert b'MACHINE FreeBSD' in rows(c,'PROFILE',b'END PROFILE')
   assert post(c,'after crash','third article')==3
   assert cmd(c,'WITHDRAW 2 CONFIRM')==b'WITHDRAW SAVED 2'
  finally:c.close();s.close()
  assert not board.exists() and not members.exists(),'SQLite operation recreated legacy primary files'
  with tempfile.TemporaryDirectory(prefix='db67-maint-') as work:
   work=Path(work);archive=work/'backup.tar.gz';restored=work/'compact'
   tool=Path(__file__).resolve().parents[1]/'scripts/maintenance.py'
   subprocess.run([sys.executable,str(tool),'db-backup','--root',str(p),'--database',str(database),'--board','notes','--members','members',str(archive)],check=True,capture_output=True)
   subprocess.run([sys.executable,str(tool),'verify',str(archive)],check=True,capture_output=True)
   result=subprocess.run([sys.executable,str(tool),'db-compact',str(archive),str(restored),'--reclaim-withdrawn','--all-stores'],capture_output=True)
   assert result.returncode==0,(result.stdout,result.stderr)
   assert b'SQLite authoritative article' not in (restored/'notes').read_bytes()
   s=Server(binary,restored/'notes',members=restored/'members',options=['--database',str(restored/'liner.sqlite')]);c=Client(s.port)
   try:
    auth(c,'alice');assert read(c,3)[2]=='third article\r\n';assert post(c,'after compact','fourth')==4
    assert cmd(c,'READ 2').startswith(b'WITHDRAWN 2')
   finally:c.close();s.close()
  with sqlite3.connect(database) as db:
   ident=db.execute("select id from streams where name='notes'").fetchone()[0]
   db.execute('delete from blocks where stream=? and number=0',(ident,))
  rejected=subprocess.run([binary,*options,'0',str(board),str(members)],capture_output=True,timeout=10)
  assert rejected.returncode!=0,'Missing committed storage block accepted'
 print('PASS stage67 authoritative DB: migration preserves originals, crash persistence, exclusive instance, no legacy dependency, missing block rejection')
if __name__=='__main__':run(sys.argv[1])
