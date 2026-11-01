"""Stage64 scope, privacy, SQLite replay and maintenance acceptance tests."""
from pathlib import Path
import sys,tempfile,subprocess,time,sqlite3,hashlib,contextlib
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_stage58 import cmd as _cmd,rows
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import notes_compact
from transfer_connectors import MailConnector,LineConnector

def cmd(c,text):
 result=_cmd(c,text);print(repr(result),flush=True);return result

def run(binary,vaultctl):
 binary,vaultctl=map(lambda p:str(Path(p).resolve()),(binary,vaultctl))
 with tempfile.TemporaryDirectory(prefix='stage64-') as tmp:
  p=Path(tmp);board=p/'notes';members=p/'members';vault=p/'vault';admins=p/'admins'
  subprocess.run([vaultctl,'init',str(vault)],check=True,capture_output=True)
  (vault/'limits.conf').write_text('max_files=0\nmax_events=0\nmax_bytes=0\n')
  def start(**extra):return Server(binary,board,members=members,vault=vault,language='en',admins=admins if admins.exists() else None,**extra)
  s=start()
  for name in ('admin','sigop','alice','bob'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close();admins.write_text('1\n');s=start();a,g,b,x=[Client(s.port) for _ in range(4)]
  try:
   for c,name in zip((a,g,b,x),('admin','sigop','alice','bob')):auth(c,name)
   for text in ('2|1|member|member|Forum|root','3|2|member|member|Child|leaf','4|1|member|member|Elsewhere|other'):
    assert cmd(a,'BOARDSET '+text).startswith(b'BOARD UPDATED'),text
   assert cmd(a,'SIGOP 2 2').startswith(b'SIGOP SET')
   g.close();g=Client(s.port);auth(g,'sigop')
   assert cmd(g,'BOARDOPTIONS 3 BOARD 1 0 0 1000')==b'BOARD OPTIONS SAVED'
   assert cmd(g,'BOARDOPTIONS 4 BOARD 1 0 0 1000')==b'ERR admin required'
   assert cmd(a,'BOARDSET 3|2|l2+g10|l2+g10|Child|leaf').startswith(b'BOARD UPDATED')
   assert cmd(g,'BOARD 3').startswith(b'BOARD 3')
   assert cmd(g,'GROUP 3 10').startswith(b'RIGHTS 3')
   assert cmd(a,'SIGOPGROUP 3 10')==b'SIGOP GROUP SAVED'
   assert cmd(g,'GROUP 3 10').startswith(b'RIGHTS 3')
   b.close();b=Client(s.port);auth(b,'alice')
   assert cmd(g,'GROUP 3 11')==b'ERR admin required'
   assert cmd(g,'GROUP 1 10')==b'ERR admin required'
   assert cmd(b,'BOARD 3').startswith(b'BOARD 3')
   first=post(b,'needle alpha','private article body')
   assert cmd(a,'BOARD 4').startswith(b'BOARD 4');other=post(a,'needle other','other article')
   assert cmd(g,'BOARD 2').startswith(b'BOARD 2')
   found=rows(g,'FIND SUBTREE * needle');assert any(l.startswith(str(first).encode()+b' ') for l in found) and not any(l.startswith(str(other).encode()+b' ') for l in found),found
   assert cmd(g,'MEMBERPROFILE 3 MACHINE bad')==b'ERR profile permission or value'
   assert cmd(a,'MEMBERPROFILE 3 MACHINE X68000')==b'PROFILE SAVED'
   assert b'MACHINE X68000' in rows(b,'PROFILE',b'END PROFILE')
   # Private mail import never grants SYSOP a read exception.
   assert cmd(b,'MAIL SEND bob sample').startswith(b'MAIL EDIT');b.send(b'private mail text\n');assert cmd(b,'.').startswith(b'MAIL SENT')
   assert cmd(b,'MAIL GET 1')==b'MAIL IMPORTED TO BUFFER'
   assert cmd(a,'MAIL GET 1')==b'MAIL ERROR unavailable'
   cmd(a,'MAIL EXIT');cmd(b,'MAIL EXIT')
   # New checkpoint only advances after the user reaches the end and commits.
   with contextlib.closing(sqlite3.connect(str(board)+'.features.sqlite')) as db:assert db.execute('select count(*) from news').fetchone()[0]==0
   b.send(b'CHECK ARTICLES\n');data=[]
   while True:
    l=b.line();data.append(l)
    if l.startswith(b'CHECK NEXT /'):break
   assert any(l.startswith(b'NEWS ') for l in data),data
   assert cmd(b,'CHECK CANCEL')==b'CHECK CANCELLED'
   b.send(b'CHECK ARTICLES\n')
   while not b.line().startswith(b'CHECK NEXT /'):pass
   assert cmd(b,'CHECK DONE')==b'CHECKPOINT SAVED'
   with contextlib.closing(sqlite3.connect(str(board)+'.features.sqlite')) as db:assert db.execute('select count(*) from news where user=3').fetchone()[0]==1
   # Vault hierarchy, password revision, rename, menu-only node.
   for arg in ('-|member|member','files|member|member','files/child|member|member','empty|member|member'):
    assert cmd(a,'FILEBOARDSET '+arg)==b'FILEBOARD UPDATED'
   assert cmd(a,'FILEBOARDSIGOP files|2')==b'FILEBOARD UPDATED'
   assert cmd(g,'FILEBOARDNAME files/child|Child')==b'FILEBOARD UPDATED'
   assert cmd(g,'FILEBOARDNAME empty|No')==b'ERR vault denied'
   assert cmd(a,'FILEBOARDOPTIONS empty|MENU 1 2')==b'FILEBOARD UPDATED'
   assert cmd(a,'FILEBOARDPASS files')==b'NEW PASSWORD';assert cmd(a,'vault-secret-64')==b'CONFIRM PASSWORD';assert cmd(a,'vault-secret-64')==b'FILEBOARD UPDATED'
   assert cmd(b,'FILEBOARDINFO files/child')==b'ERR vault denied'
   assert cmd(b,'FILEUNLOCK files')==b'PASSWORD';assert cmd(b,'vault-secret-64').startswith(b'FILEBOARD UNLOCKED')
   assert rows(b,'FILEBOARDINFO files/child',b'END FILEBOARD')[0].startswith(b'FILEBOARDSETTING')
   # Delete does not renumber articles; management-tool copy removes the bytes.
   assert cmd(b,'BOARD 3').startswith(b'BOARD 3');assert cmd(b,f'WITHDRAW {first} CONFIRM').startswith(b'WITHDRAW SAVED')
  finally:
   for c in (a,g,b,x):c.close()
   s.close()
  output=p/'reclaimed';report=notes_compact.compact('board',board,output,True)
  assert report['reclaimed_articles']==1 and b'private article body' not in output.read_bytes()
  assert b'private article body' in board.read_bytes()
  # Rebuild indexes on restart and retain scoped roles and checkpoint.
  s=start();a=Client(s.port);g=Client(s.port)
  try:
   auth(a,'admin');auth(g,'sigop');assert cmd(g,'BOARDOPTIONS 3 BOARD 1 0 0 1000')==b'BOARD OPTIONS SAVED'
   assert cmd(a,'BOARDGROUP 3 10 11 CONFIRM')==b'BOARD GROUP MOVED'
  finally:a.close();g.close();s.close()
  print('PASS stage64 inherited/scoped roles, subtree search, private import, checkpoints, vault passwords, profiles, reclaim and replay')
if __name__=='__main__':run(*sys.argv[1:])



