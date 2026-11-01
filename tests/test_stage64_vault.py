"""Vault hierarchy, per-board receipts, password rotation and menu-node isolation."""
from pathlib import Path
import sys,tempfile,subprocess,hashlib,time
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_stage58 import cmd,rows,receive,counts
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from transfer_connectors import LineConnector,RemoteError

def run(binary,ctl):
 binary,ctl=[str(Path(v).resolve()) for v in (binary,ctl)]
 with tempfile.TemporaryDirectory(prefix='stage64-vault-') as d:
  p=Path(d);board=p/'notes';users=p/'members';admins=p/'admins';vault=p/'vault'
  subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True);(p/'tiny').write_bytes(b'abc')
  subprocess.run([ctl,'import',str(vault),str(p/'tiny'),'original.txt','original'],check=True,capture_output=True)
  def start():return Server(binary,board,members=users,admins=admins if admins.exists() else None,vault=vault,language='en')
  s=start()
  for n in ('admin','sigop','reader'):
   c=Client(s.port);auth(c,n,True);c.close()
  s.close();admins.write_text('1\n');s=start();a,g,b=[Client(s.port) for _ in range(3)];api=None
  try:
   for c,n in zip((a,g,b),('admin','sigop','reader')):auth(c,n)
   for path in ('-','parent','parent/child','other','menu'):assert cmd(a,'FILEBOARDSET '+path+'|member|member')==b'FILEBOARD UPDATED'
   assert cmd(a,'FILECATEGORY 1 parent/child')==b'FILE CATEGORY 1'
   assert cmd(a,'FILEBOARDSIGOP parent|2')==b'FILEBOARD UPDATED'
   assert cmd(g,'FILERENAME 1 renamed.txt')==b'FILE RENAMED 1'
   assert cmd(b,'FILERENAME 1 denied.txt')==b'ERR vault denied'
   assert cmd(g,'FILERENAME 1 ../escape')==b'ERR vault syntax'
   assert cmd(g,'FILEDESC 1 edited')==b'FILE EDITED 1'
   assert cmd(g,'FILEBOARDOPTIONS parent/child|BOARD 2 2')==b'FILEBOARD UPDATED'
   api=LineConnector('127.0.0.1',s.port,'reader',PASSWORD)
   for _ in range(5):receive(api,1);api.complete_download(1)
   assert counts(a)==5
   history=rows(a,'FILEHISTORY 1');assert sum(v.startswith(b'DOWNLOADROW ') for v in history)==2,history
   assert cmd(a,'FILEBOARDOPTIONS menu|MENU 1 0')==b'FILEBOARD UPDATED'
   api.upload_category='menu'
   try:api.begin(0,hashlib.sha256(b'').hexdigest(),'denied','')
   except RemoteError:pass
   else:raise AssertionError('Menu upload allowed')
   assert cmd(a,'FILEBOARDPASS parent')==b'NEW PASSWORD';cmd(a,'Password-parent');assert cmd(a,'Password-parent')==b'FILEBOARD UPDATED'
   try:api.info(1)
   except RemoteError:pass
   else:raise AssertionError('Locked file accessible')
   assert cmd(b,'FILEUNLOCK parent')==b'PASSWORD';assert cmd(b,'Password-parent').startswith(b'FILEBOARD UNLOCKED')
   assert b'NAME renamed.txt' in rows(b,'FILE 1',b'END FILE')
   assert cmd(b,'LOGOUT')==b'LOGGED OUT';time.sleep(1.05);auth(b,'reader');assert cmd(b,'FILE 1')==b'ERR no file'
   assert cmd(b,'FILEUNLOCK parent')==b'PASSWORD';cmd(b,'Password-parent')
   assert cmd(a,'FILEBOARDPASS parent')==b'NEW PASSWORD';cmd(a,'Password-changed');assert cmd(a,'Password-changed')==b'FILEBOARD UPDATED'
   assert cmd(b,'FILE 1')==b'ERR no file'
   assert b'NAME renamed.txt' in rows(g,'FILE 1',b'END FILE')
  finally:
   if api:api.close()
   for c in (a,g,b):c.close()
   s.close()
  s=start();a=Client(s.port);g=Client(s.port);b=Client(s.port)
  try:
   auth(a,'admin');auth(g,'sigop');auth(b,'reader');assert counts(a)==5
   assert sum(v.startswith(b'DOWNLOADROW ') for v in rows(a,'FILEHISTORY 1'))==2
   assert cmd(b,'FILE 1')==b'ERR no file'
   assert cmd(g,'FILEDELETE 1 CONFIRM')==b'FILE DELETED 1';assert not (vault/'00000001.bin').exists()
  finally:a.close();g.close();b.close();s.close()
 print('PASS stage64 inherited vault management, rename, retained receipts and totals, menu nodes, parent password revision/logout/restart, physical deletion')
if __name__=='__main__':run(*sys.argv[1:])
