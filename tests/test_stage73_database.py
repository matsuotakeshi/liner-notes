"""Copy-only conversion, native structured backup/restore and source preservation."""
from pathlib import Path
import hashlib,sqlite3,subprocess,sys,tempfile
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_mail import send
from test_stage58 import cmd

ROOT=Path(__file__).resolve().parents[1]

def run(binary):
 binary=str(Path(binary).resolve())
 def admin(*args,success=True):
  r=subprocess.run([sys.executable,str(ROOT/'scripts/maintenance.py'),*map(str,args)],capture_output=True)
  assert (r.returncode==0)==success,(r.stdout,r.stderr)
  return r
 with tempfile.TemporaryDirectory(prefix='db73-') as tmp:
  p=Path(tmp);source=p/'source';source.mkdir();db=source/'liner.sqlite'
  s=Server(binary,source/'notes',members=source/'members',options=['--database',str(db)]);c=Client(s.port)
  try:
   auth(c,'alice',True);post(c,'日本語','body 1');post(c,'large','x'*3000)
   send(c,'alice','mail','private body');cmd(c,'PROFILESET MACHINE FreeBSD')
   cmd(c,'WITHDRAW 1 CONFIRM')
   admin('db-structure','--root',source,'--database',db,'--board','notes','--members','members',p/'locked',success=False)
  finally:c.close();s.close()
  before=hashlib.sha256(db.read_bytes()).digest()
  copy=p/'structured'
  admin('db-structure','--root',source,'--database',db,'--board','notes','--members','members',copy)
  assert hashlib.sha256(db.read_bytes()).digest()==before
  admin('db-structure','--root',source,'--database',db,'--board','notes','--members','members',copy,success=False)
  for round in range(2):
   current=copy if round==0 else p/'restored'
   with sqlite3.connect(current/'liner.sqlite') as d:
    assert d.execute('SELECT version FROM storage_meta').fetchone()==(3,)
    assert d.execute('SELECT count(*) FROM article_records').fetchone()[0]==2
    assert d.execute('SELECT count(*) FROM blocks WHERE stream IN (SELECT stream FROM record_streams)').fetchone()==(0,)
    assert d.execute('PRAGMA integrity_check').fetchone()==('ok',)
   s=Server(binary,current/'notes',members=current/'members',options=['--database-structured',str(current/'liner.sqlite')]);c=Client(s.port)
   try:
    auth(c,'alice');assert read(c,2)[2]=='x'*3000+'\r\n'
    assert cmd(c,'READ 1').startswith(b'WITHDRAWN 1')
    assert cmd(c,'MAIL GET 1')==b'MAIL IMPORTED TO BUFFER'
   finally:c.close();s.close()
   if round==0:
    archive=p/'backup.tar.gz'
    admin('db-backup','--root',current,'--database',current/'liner.sqlite','--board','notes','--members','members',archive)
    admin('db-restore',archive,p/'restored')
  assert hashlib.sha256(db.read_bytes()).digest()==before
 print('PASS stage73 database: copy-only conversion, row storage, auth/read/mail/restart, structured restore, source hashes and live lock')

if __name__=='__main__':run(sys.argv[1])
