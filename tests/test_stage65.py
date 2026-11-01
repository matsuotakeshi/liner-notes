"""Stage65 real-socket permission, author audit, branch and deletion restart tests."""
from pathlib import Path
import sys,tempfile,subprocess,time
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd as rawcmd,rows
def cmd(c,s):
 r=rawcmd(c,s);print(s,repr(r),flush=True);return r

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='ln65-') as tmp:
  p=Path(tmp);board=p/'notes';members=p/'members';admins=p/'admins';clients=[]
  def start():return Server(binary,board,members=members,admins=admins if admins.exists() else None,language='en')
  s=start()
  for name in ('admin','sigop','alice'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close();admins.write_text('1\n');s=start()
  try:
   a,g,u,v=[Client(s.port) for _ in range(4)];clients=[a,g,u,v]
   for c,name in ((a,'admin'),(g,'sigop'),(u,'alice')):auth(c,name)
   for value in ('2|1|all|all|Public|public','3|1|member|member|Other|other','4|2|all|all|Child|child'):
    assert cmd(a,'BOARDSET '+value).startswith(b'BOARD UPDATED')
   assert cmd(a,'SIGOP 2 2').startswith(b'SIGOP SET')
   g.close();g=Client(s.port);auth(g,'sigop');clients[1]=g
   assert cmd(g,'BOARDGUEST 3 ON')==b'ERR admin required'
   assert cmd(g,'boardguest 2 on')==b'BOARD GUEST SAVED'
   assert cmd(v,'HANDLE visitor')==b'GUEST HANDLE guest:visitor'
   assert cmd(v,'BOARD 2').startswith(b'BOARD 2')
   first=post(v,'guest note','anonymous body')
   v.send(f'READ {first}\n'.encode());assert b'AUTHOR guest:visitor' in v.line()
   while v.line()!=b'END':pass
   assert cmd(a,'BOARD 2').startswith(b'BOARD 2')
   child=post(a,'child','reply',first);leaf=post(a,'leaf','leaf',child)
   assert cmd(u,f'AUTHOR {first} 3 CONFIRM')==b'ERR admin required'
   assert cmd(g,f'author {first} 3 confirm').startswith(b'AUTHOR CHANGED')
   assert cmd(g,'BOARD 2').startswith(b'BOARD 2')
   assert any(b'AUTHOR' in x for x in rows(g,f'AUDIT ARTICLE {first}'))
   assert cmd(a,f'MOVEBRANCH {child} 3 CONFIRM').startswith(b'THREAD MOVED')
   assert cmd(a,'BOARD 3').startswith(b'BOARD 3');assert read(a,child)[0]==0;assert read(a,leaf)[0]==child
   assert cmd(a,'MENUMODE EXPERT')==b'MENU MODE SAVED'
   assert b'ja' in cmd(a,'LANG ja');assert cmd(a,'PAGER ON')==b'PAGER ON'
   assert cmd(a,'LOGOUT')==b'LOGGED OUT';auth(a,'admin')
   assert cmd(a,'PREFS')==b'PREFS STORED ja ON'
   for c in clients:c.close()
   clients=[];s.close();s=start();a=Client(s.port);clients=[a];auth(a,'admin')
   assert cmd(a,'BOARD 3').startswith(b'BOARD 3');assert read(a,child)[0]==0
   assert cmd(a,'BOARDDELETEALL 2 CASCADE CONFIRM')==b'BOARD TREE DELETED'
  finally:
   for c in clients:c.close()
   s.close()
  s=start();a=Client(s.port)
  try:
   auth(a,'admin');assert cmd(a,'BOARD 2').startswith(b'ERR');assert cmd(a,'BOARD 4').startswith(b'ERR')
   assert cmd(a,'BOARD 3').startswith(b'BOARD 3');assert read(a,leaf)[0]==child
  finally:a.close();s.close()
  # Compact a stopped copy with the new author and branch events; preserve originals.
  root=Path(__file__).resolve().parents[1];snapshot=p/'snapshot'
  import shutil
  snapshot.mkdir()
  for f in list(p.iterdir()):
   if f.is_file():shutil.copy2(f,snapshot/f.name)
  archive=p/'backup.tar';compact=p/'compact'
  subprocess.run([sys.executable,str(root/'scripts/maintenance.py'),'backup','--root',str(snapshot),'--board','notes','--members','members',str(archive)],check=True,stdout=subprocess.DEVNULL)
  subprocess.run([sys.executable,str(root/'scripts/maintenance.py'),'compact',str(archive),str(compact),'--all-stores','--reclaim-withdrawn','--tools-dir',str(root)],check=True,stdout=subprocess.DEVNULL)
  s=Server(binary,compact/'notes',members=compact/'members',admins=compact/'admins');a=Client(s.port)
  try:
   auth(a,'admin');assert cmd(a,'BOARD 3').startswith(b'BOARD 3');assert read(a,leaf)[0]==child
   assert cmd(a,'BOARD 2').startswith(b'ERR')
  finally:a.close();s.close()
 print('stage65 core permissions/restart/compaction passed')
if __name__=='__main__':run(sys.argv[1])
