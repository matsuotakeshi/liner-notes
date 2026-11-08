"""Ancestor visibility on ordinary boards, including counts and deep threads."""
import sys,tempfile,hashlib
from pathlib import Path
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,unread
from test_moderation import expect,inspect
from test_notes import page,navigate

def run(binary):
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp);s=Server(binary,p/'notes',members=p/'members')
  a=Client(s.port);auth(a,'admin',True);a.close();s.close()
  (p/'admins').write_text('1\n')
  s=Server(binary,p/'notes',members=p/'members',admins=p/'admins')
  a,b=Client(s.port),Client(s.port)
  try:
   auth(a,'admin');auth(b,'bob',True)
   root=post(b,'root-secret','root-body-secret')
   hidden=post(b,'child-secret','child-body-secret',root)
   grand=post(b,'grand-secret','grand-body-secret',hidden)
   sibling=post(b,'visible-sibling','visible-body',root)
   expect(a,f'HIDE {hidden}',f'HIDDEN SAVED {hidden}')
   h,rows,_=page(b,'NOTES');assert h==b'NOTES 1' and b'replies=1 latest=4' in rows[0],(h,rows)
   read(b,root);navigate(b,'CHILD',sibling)
   expect(b,'PREV','ERR no neighbour')
   assert unread(b)==[root,sibling]
   expect(a,f'RESTORE {hidden}',f'RESTORED {hidden}')
   expect(b,f'WITHDRAW {root} CONFIRM',f'WITHDRAW SAVED {root}')
   expect(a,f'RESTORE {root}','ERR article withdrawn')
   for ident in (hidden,grand,sibling):expect(b,f'READ {ident}',f'HIDDEN {ident} PARENT {root if ident!=grand else hidden}')
   for command in ('LIST','NOTES','TREE 1','SEARCH secret','FIND ALL * secret','NEWS ALL 2000-01-01'):
    h,rows,_=page(b,command);assert not rows,(command,h,rows)
   assert unread(b)==[]
   expect(b,'GET 3','ERR no article')
   expect(b,'REPLY 3 denied','ERR article hidden')
   header,title,body=inspect(a,grand);assert b'HIDDEN 1' in header and body==b'grand-body-secret\r\n'
   # Deep topology exercises iterative visibility and invalidation on restore.
   first=post(b,'deep-root','deep-body');parent=first
   for _ in range(128):parent=post(b,'deep-child','deep-body',parent)
   expect(a,f'HIDE {first}',f'HIDDEN SAVED {first}')
   expect(b,f'READ {parent}',f'HIDDEN {parent} PARENT {parent-1}')
   expect(a,f'RESTORE {first}',f'RESTORED {first}')
   assert read(b,parent)[2]=='deep-body\r\n'
   print('PASS ordinary-board withdrawal/hide cascade, visible counts/navigation, audit and deep topology')
  finally:a.close();b.close();s.close()
  # Reclaim only the withdrawn parent's body in a separate restored copy.
  # Descendant data and audit survive; restoring a child cannot republish it.
  sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
  import maintenance
  original=hashlib.sha256((p/'notes').read_bytes()).digest()
  with tempfile.TemporaryDirectory() as maintenance_tmp:
   dest=Path(maintenance_tmp);archive=dest/'backup.tar.gz';copy=dest/'copy'
   maintenance.backup(p,'notes','members',archive)
   maintenance.restore(archive,copy,compact=True,reclaim_withdrawn=True)
   assert hashlib.sha256((p/'notes').read_bytes()).digest()==original
   assert b'root-body-secret' not in (copy/'notes').read_bytes()
   assert b'grand-body-secret' in (copy/'notes').read_bytes()
   s=Server(binary,copy/'notes',members=copy/'members',admins=copy/'admins')
   a=Client(s.port)
   try:
    auth(a,'admin')
    expect(a,'READ 1','WITHDRAWN 1 PARENT 0')
    expect(a,'RESTORE 3','RESTORED 3')
    expect(a,'READ 3','HIDDEN 3 PARENT 2')
    assert inspect(a,3)[2]==b'grand-body-secret\r\n'
    assert post(a,'after reclaim','retained ids')==parent+1
    _,history,_=page(a,'AUDIT ARTICLE 1')
    assert any(b'WITHDRAW' in line for line in history),history
   finally:a.close();s.close()
  print('PASS withdrawn-body reclamation preserves descendants, audit and monotonic IDs')
if __name__=='__main__':run(str(Path(sys.argv[1]).resolve()))
