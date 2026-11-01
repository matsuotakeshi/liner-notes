"""Interactive Telnet flows, including returning from auxiliary editor commands."""
from pathlib import Path
import sys,tempfile,subprocess,time,re,datetime
from test_board import Server
from test_runtime import Client
from test_telnet import Client as Telnet
from test_members import auth,PASSWORD
from test_stage58 import cmd
ROOT=Path(__file__).resolve().parents[1]

def run(binary,ctl):
 binary,ctl=map(lambda s:str(Path(s).resolve()),(binary,ctl))
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp);notes=p/'notes';users=p/'users';v=p/'vault';admins=p/'admins'
  subprocess.run([ctl,'init',str(v)],check=True,capture_output=True)
  def cli(*args):subprocess.run([ctl,*map(str,args)],check=True,capture_output=True)
  for path in ('-','files','files/child','target'):cli('board-set',v,path,'member','member')
  src=p/'src';src.write_text('file contents\n');cli('import-in',v,'files',src,'readme.txt','file description')
  for name in ('welcome','systemnews','birthday'):(p/(name+'.txt')).write_text(name.upper()+' MESSAGE\n')
  s=Server(binary,notes,members=users);a,b=Client(s.port),Client(s.port)
  auth(a,'admin',True);auth(b,'bob',True)
  today=datetime.datetime.now(datetime.timezone.utc).strftime('%m-%d');assert cmd(b,'PROFILESET BIRTHDAY 2000-'+today)==b'PROFILE SAVED'
  a.close();b.close();s.close();admins.write_text('1\n')
  s=Server(binary,notes,members=users,vault=v,admins=admins,menu=True,telnet=True,language='en',options=['--login-text-dir',str(p)])
  a,b=Telnet(s.port),Telnet(s.port)
  def ui(c,text):return c.cmd(text,b'> ')
  try:
   first=a.until();second=b.until();assert b'WELCOME MESSAGE' in first and b'SYSTEMNEWS MESSAGE' in first
   aid=first.split(b'RTBBS-PROBE ')[1].split()[0].decode();bid=second.split(b'RTBBS-PROBE ')[1].split()[0].decode()
   ui(a,'LOGIN admin');ui(a,PASSWORD);ui(b,'LOGIN bob');birth=ui(b,PASSWORD);assert b'BIRTHDAY MESSAGE' in birth,birth
   ui(a,'PAGER OFF');ui(b,'PAGER OFF')
   ui(a,'P');assert b'Field' in ui(a,'E')+ui(a,'2');assert b'Value' in ui(a,'8');assert b'saved' in ui(a,'X68000').lower()
   ui(a,'MENU');ui(a,'P');ui(a,'S');ui(a,'8');result=ui(a,'X68000');assert b'MACHINE X68000' in result,result
   # Last matching member may have a next cursor even when the rest do not match.
   if b'Next:Enter' in result:ui(a,'0')
   ui(a,'MENU');ui(a,'VAULT');ui(a,'FILEBOARD files');assert b'CHILD 1 files/child' in ui(a,'DIR')
   assert b'files/child' in ui(a,'1');assert b'files|' in ui(a,'0')
   assert b'readme.txt' in ui(a,'L');selected=ui(a,'1');assert b'T:view' in selected and b'file description' in selected,selected
   assert b'BATCH FILE 1 PENDING' in ui(a,'b');ui(a,'0')
   ui(a,'B');assert b'BATCH FILE 1 PENDING' in ui(a,'L');ui(a,'C');result=ui(a,'N');assert b'BATCH' in result
   ui(a,'M');result=ui(a,'target');assert b'FILE COPIED 1 2' in result,result
   assert b'BATCH COPY 1 DONE' in ui(a,'L');ui(a,'0');ui(a,'MENU')
   ui(a,'BUFFER');ui(a,'/EDITOR LINE');ui(a,'A');ui(a,'one needle needle');ui(a,'two needle');ui(a,'three needle');ui(a,'//')
   result=ui(a,'S2-2 needle');assert b'Matches: 1' in result and b'one needle' not in result,result
   result=ui(a,'S1-2 needle');assert b'Matches: 3' in result and result.count(b'\x1b[33m')==3,result
   ui(a,'M');result=ui(a,'L');assert b'1 one needle' in result,result
   ui(a,'M');result=ui(a,'L');assert b'1 one needle' not in result and b'one needle' in result,result
   ui(a,'\\');assert b'updated' in ui(a,'MixedCaseMemo').lower()
   result=ui(a,'W');assert b'MixedCaseMemo' in result and b'bob' in result,result
   result=ui(a,'#');assert b'STATUS' in result and b'SECONDS' in result,result
   ui(a,'T '+bid);result=ui(a,'PrivateMixedCase');assert b'TEXT EDIT' in result,result
   # Auxiliary talk returns to the same draft, and retains body case/content.
   result=ui(a,'L');assert b'one needle needle' in result and b'PrivateMixedCase' not in result,result
   ui(a,'T '+bid);assert b'TEXT EDIT' in ui(a,'//');ui(a,'/CANCEL')
   ui(a,'MENU');ui(a,'CHAT');result=ui(a,'W');assert b'bob' in result,result # Bob is outside chat.
   result=ui(a,'#');assert b'STATUS' in result and b'ONLINE' in result,result
   time.sleep(.55);ui(a,'T '+bid);result=ui(a,'OneShotPrivate');assert b'Chat>' in result,result
   time.sleep(.55);result=ui(a,'PublicAfterTalk');assert b'PublicAfterTalk' in result,result
   ui(a,'//')
  finally:a.close();b.close();s.close()
  # New automatic ID format and confirmation precede account allocation.
  s=Server(binary,p/'newnotes',members=p/'newusers',menu=True,telnet=True,language='en',options=['--id-mode','auto','--id-prefix','bbs','--id-digits','5','--registration-form','detailed'])
  a=Telnet(s.port)
  try:
   a.until();a.cmd('REGISTER',b': ')
   for i in range(14):a.cmd('MixedName' if i==6 else '',b'> ' if i==13 else b': ')
   assert b'Y / E' in a.cmd('N',b'> ')
   ui(a,'Y');result=ui(a,PASSWORD);assert b'bbs00001' in result,result
  finally:a.close();s.close()
 print('PASS stage69 Telnet file/batch/hierarchy, profile forms, editor ranges/highlights/numbers, memo, W/#/one-shot T, birthday and registration confirmation')

if __name__=='__main__':run(*sys.argv[1:])
