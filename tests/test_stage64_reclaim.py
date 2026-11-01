"""Offline reclaim removes article and FTS copies, preserving backups and state."""
from pathlib import Path
import sys,tempfile,subprocess,hashlib,sqlite3
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd
ROOT=Path(__file__).resolve().parents[1]

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='reclaim64-') as tmp:
  p=Path(tmp);source=p/'data';source.mkdir();board=source/'notes';members=source/'members';admins=source/'admins'
  s=Server(binary,board,members=members);c=Client(s.port)
  try:
   auth(c,'alice',True);assert post(c,'withdrawn','body-must-be-physically-removed')==1
   assert post(c,'hidden','hidden-body-retained')==2
   assert cmd(c,'WITHDRAW 1 CONFIRM')==b'WITHDRAW SAVED 1'
  finally:c.close();s.close()
  admins.write_text('1\n');s=Server(binary,board,members=members,admins=admins);c=Client(s.port)
  try:auth(c,'alice');assert cmd(c,'HIDE 2')==b'HIDDEN SAVED 2'
  finally:c.close();s.close()
  with sqlite3.connect(str(board)+'.features.sqlite') as db:db.execute('insert into news values(1,0,123)')
  before={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in source.iterdir() if f.is_file()}
  def tool(*args):
   r=subprocess.run([sys.executable,str(ROOT/'scripts/maintenance.py')]+list(map(str,args)),capture_output=True,timeout=60)
   assert r.returncode==0,r.stderr
  archive=p/'backup.tar.gz';restored=p/'restored'
  tool('backup','--root',source,'--board','notes','--members','members',archive)
  saved=hashlib.sha256(archive.read_bytes()).hexdigest()
  tool('compact',archive,restored,'--reclaim-withdrawn')
  assert not list(restored.glob('*-index.sqlite*'))
  assert b'body-must-be-physically-removed' not in (restored/'notes').read_bytes()
  assert b'hidden-body-retained' in (restored/'notes').read_bytes()
  assert before=={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in source.iterdir() if f.is_file()}
  assert hashlib.sha256(archive.read_bytes()).hexdigest()==saved
  with sqlite3.connect(restored/'notes.features.sqlite') as db:assert db.execute('select stamp from news').fetchone()==(123,)
  s=Server(binary,restored/'notes',members=restored/'members',admins=restored/'admins');c=Client(s.port)
  try:
   auth(c,'alice');response=cmd(c,'READ 1');assert response.startswith(b'WITHDRAWN 1 PARENT '),response
   assert cmd(c,'RESTORE 2')==b'RESTORED 2';assert read(c,2)[2]=='hidden-body-retained\r\n'
   assert post(c,'next','new')==3
  finally:c.close();s.close()
  assert b'body-must-be-physically-removed' not in (restored/'notes.article-index.sqlite').read_bytes()
 print('PASS: management CLI backup/reclaim/restart; withdrawn body and old FTS removed, HIDE preserved, IDs and checkpoint retained, originals unchanged')
if __name__=='__main__':run(sys.argv[1])
