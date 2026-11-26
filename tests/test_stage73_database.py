"""Copy-only conversion, native structured backup/restore and source preservation."""
from pathlib import Path
from contextlib import closing
import hashlib,os,sqlite3,subprocess,sys,tempfile
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_mail import send,read as mail_read
from test_talk import command
from test_stage58 import cmd
from transfer_connectors import MailConnector

ROOT=Path(__file__).resolve().parents[1]

def run_version(binary,version):
 binary=str(Path(binary).resolve())
 def admin(*args,success=True):
  r=subprocess.run([sys.executable,str(ROOT/'scripts/maintenance.py'),*map(str,args)],capture_output=True)
  assert (r.returncode==0)==success,(r.stdout,r.stderr)
  return r
 with tempfile.TemporaryDirectory(prefix='db73-') as tmp:
  p=Path(tmp);parent=p/'protected-parent';parent.mkdir();source=parent/'source';source.mkdir();db=source/'liner.sqlite'
  if version>1:
   with closing(sqlite3.connect(db)) as seed, seed:
    seed.executescript('CREATE TABLE storage_meta(id INTEGER PRIMARY KEY,version INTEGER NOT NULL,uuid BLOB NOT NULL);')
    seed.execute('INSERT INTO storage_meta VALUES(1,?,randomblob(16))',(version,))
  s=Server(binary,source/'notes',members=source/'members',options=['--database',str(db),'--ui-policy','MEMBERS']);c=Client(s.port)
  try:
   auth(c,'alice',True)
   # Seed the migration setting at the main prompt, before posting selects a
   # board. Leaving mail alone does not leave that previously selected board.
   reply=cmd(c,'UI RTBBS');assert reply==b'UI PREF SAVED rtbbs',reply
   assert not cmd(c,'LANG en').startswith(b'ERR ')
   reply=cmd(c,'PREFS SAVE');assert reply==b'PREFS SAVED en OFF',reply
   post(c,'日本語','body 1');post(c,'large','x'*3000)
   send(c,'alice','mail','private body')
   assert cmd(c,'0')==b'MAIL CLOSED'
   reply=cmd(c,'PROFILESET MACHINE FreeBSD');assert reply==b'PROFILE SAVED',reply
   files=MailConnector('127.0.0.1',s.port,'alice',PASSWORD)
   attached=[]
   try:
    for i in range(3):
     payload=('attachment '+str(i)).encode();digest=hashlib.sha256(payload).hexdigest()
     ident=files.begin(len(payload),digest,'file'+str(i)+'.txt','')
     files.write(ident,0,payload);files.finish(ident);attached.append((ident,payload,digest))
   finally:files.close()
   command(c,'MAIL SEND alice relations',b'MAIL EDIT ')
   for ident,_,_ in attached:command(c,'/ATTACH '+str(ident),b'MAIL ATTACHMENT ')
   c.send(b'attachment body\n');command(c,'.',b'MAIL SENT 2')
   assert cmd(c,'0')==b'MAIL CLOSED'
   reply=cmd(c,'WITHDRAW 1 CONFIRM');assert reply==b'WITHDRAW SAVED 1',reply
   admin('db-structure','--root',source,'--database',db,'--board','notes','--members','members',p/'locked',success=False)
  finally:c.close();s.close()
  with closing(sqlite3.connect(db)) as original:
   saved=original.execute('SELECT user,profile FROM ui_preferences ORDER BY user').fetchall()
   assert saved==[(1,'rtbbs')],('source UI preference',version,saved)
  companion='liner.sqlite-operator-notes'
  (source/companion).write_bytes(b'operator notes\x00\xff')
  before=hashlib.sha256(db.read_bytes()).digest()
  copy=p/'structured'
  # A service account may own its data directory while /var/db is read-only.
  if os.name!='nt':parent.chmod(0o555)
  try:admin('db-structure','--root',source,'--database',db,'--board','notes','--members','members',copy)
  finally:
   if os.name!='nt':parent.chmod(0o700)
  assert hashlib.sha256(db.read_bytes()).digest()==before
  admin('db-structure','--root',source,'--database',db,'--board','notes','--members','members',copy,success=False)
  for round in range(2):
   current=copy if round==0 else p/'restored'
   assert (current/companion).read_bytes()==(source/companion).read_bytes()
   with closing(sqlite3.connect(current/'liner.sqlite')) as d:
    assert d.execute('SELECT version FROM storage_meta').fetchone()==(5,)
    assert d.execute('SELECT count(*) FROM article_records').fetchone()[0]==2
    assert d.execute('SELECT count(*) FROM blocks WHERE stream IN (SELECT stream FROM record_streams)').fetchone()==(0,)
    assert d.execute('PRAGMA integrity_check').fetchone()==('ok',)
    assert d.execute('PRAGMA foreign_key_check').fetchall()==[]
    assert d.execute('SELECT count(*) FROM blocks').fetchone()==(0,)
    assert d.execute('SELECT count(*) FROM vault_records').fetchone()[0]>=6
    assert d.execute('SELECT sum(length(attributes)) FROM member_records').fetchone()==(0,)
    saved=d.execute('SELECT user,profile FROM ui_preferences ORDER BY user').fetchall()
    assert saved==[(1,'rtbbs')],('converted/restored UI preference',version,round,saved)
    from normalized_fields import validate_relations
    validate_relations(d)
    assert d.execute('SELECT file_id FROM mail_attachments ORDER BY ordinal').fetchall()==[(x[0],) for x in attached]
   s=Server(binary,current/'notes',members=current/'members',options=['--database-structured',str(current/'liner.sqlite')]);c=Client(s.port)
   try:
    auth(c,'alice');assert read(c,2)[2]=='x'*3000+'\r\n'
    assert cmd(c,'READ 1').startswith(b'WITHDRAWN 1')
    assert b'attachment body' in mail_read(c,2)
    files=MailConnector('127.0.0.1',s.port,'alice',PASSWORD)
    try:
     for ident,payload,digest in attached:
      assert files.info(ident)==(len(payload),digest);assert files.read(ident,0)==payload
    finally:files.close()
    assert send(c,'alice','after migration','new body')==3+round
    assert cmd(c,'MAIL GET 1')==b'MAIL IMPORTED TO BUFFER'
   finally:c.close();s.close()
   if round==0:
    archive=p/'backup.tar.gz'
    admin('db-backup','--root',current,'--database',current/'liner.sqlite','--board','notes','--members','members',archive)
    admin('db-restore',archive,p/'restored')
  assert hashlib.sha256(db.read_bytes()).digest()==before
 print('PASS source schema',version,'stage73 database: copy-only conversion, row storage, auth/read/mail/restart, structured restore, source hashes and live lock')

def run(binary):
 for version in (1,2,3,4,5):run_version(binary,version)

if __name__=='__main__':run(sys.argv[1])
