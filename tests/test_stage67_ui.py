"""Telnet reading, prompts, assets, scoped vault listings and permissions."""
from pathlib import Path
import sys,tempfile,subprocess,time,sqlite3
from test_board import Server,post
from test_runtime import Client
from test_telnet import Client as Telnet
from test_members import auth,PASSWORD
from test_mail import send
from test_stage58 import cmd
ROOT=Path(__file__).resolve().parents[1]

def run(binary,ctl):
 binary,ctl=map(lambda s:str(Path(s).resolve()),(binary,ctl))
 with tempfile.TemporaryDirectory(prefix='stage67-ui-') as tmp:
  p=Path(tmp);board=p/'notes';users=p/'members';vault=p/'vault';admins=p/'admins';info=p/'info';bye=p/'bye'
  info.write_text('SITE INFO 67\n');bye.write_text('GOODBYE 67\n');admins.write_text('1\n')
  def cli(*args):subprocess.run([ctl,*map(str,args)],check=True,capture_output=True)
  cli('init',vault)
  for path in ('-','games','games/action','games2'):cli('board-set',vault,path,'member','member')
  source=p/'source';source.write_bytes(b'file')
  cli('import-in',vault,'games/action',source,'child.zip','child scope')
  cli('import-in',vault,'games2',source,'outside.zip','not in scope')
  cli('import-in',vault,'games',source,'readme.txt','not a zip')
  s=Server(binary,board,members=users);a,b=Client(s.port),Client(s.port)
  try:
   auth(a,'admin',True);auth(b,'bob',True)
   post(a,'buffer source','COMMON BUFFER BODY')
   send(a,'bob','first mail','FIRST MAIL BODY');send(a,'bob','second mail','SECOND MAIL BODY')
  finally:a.close();b.close();s.close()
  options=['--sysinfo',str(info),'--goodbye',str(bye),'--timezone','Asia/Tokyo','--help-dir',str(ROOT/'help')]
  s=Server(binary,board,members=users,admins=admins,vault=vault,menu=True,telnet=True,language='en',menu_dir=ROOT/'menus',options=options)
  a,c=Telnet(s.port),Telnet(s.port)
  try:
   a.until();c.until();a.cmd('LOGIN admin',b'> ');a.cmd(PASSWORD);c.cmd('LOGIN bob',b'> ');c.cmd(PASSWORD)
   a.cmd('PAGER OFF');c.cmd('PAGER OFF')
   assert b'SITE INFO 67' in c.cmd('S');assert b'0.71' in c.cmd('V')
   assert b'Administrator' not in c.cmd('MENU')
   assert b'admin' in c.cmd('HELP ADMINSTORAGE').lower()
   assert b'1' in a.cmd('$');assert b'db-backup' in a.cmd('5');a.cmd('MENU')
   c.cmd('I');settings=c.cmd('D');assert b'Asia/Tokyo' in settings and b'ZMODEM' in settings,settings;c.cmd('MENU')
   c.cmd('READ 1');assert b'shared edit buffer' in c.cmd('G')
   c.cmd('BUFFER',b'Body > ');assert b'COMMON BUFFER BODY' in c.cmd('/PREVIEW',b'Body > ');c.cmd('/CANCEL')
   def mail(command):return c.cmd(command,b'0 exit)> ')
   mail('MAIL');assert b'FIRST MAIL BODY' in mail('1')
   assert b'FIRST MAIL BODY' in mail('L')
   assert b'SECOND MAIL BODY' in mail('')
   assert b'FIRST MAIL BODY' in mail('P')
   assert b'buffer' in mail('G').lower()
   assert b'FIRST MAIL BODY' not in mail('MAIL LIST')
   c.cmd('MAIL EXIT');c.cmd('VAULT');c.cmd('FILEBOARD games')
   result=c.cmd('S *.zip');assert b'child.zip' in result and b'outside.zip' not in result and b'readme.txt' not in result,result
   result=c.cmd('FILENEWS games|2000-01-01');assert b'child.zip' in result and b'outside.zip' not in result,result
   assert b'games' in c.cmd('I');assert b'ZMODEM' in c.cmd('U',b'> ')
   c.cmd('xmodem',b'> ');c.cmd('description',b'> ');c.cmd('name.txt',b'> ');c.cmd('/CANCEL')
   assert b'1' in c.cmd('B',b'> ');c.cmd('0');c.cmd('MENU')
   assert b'Y' in c.cmd('O',b'> ');assert b'cancel' in c.cmd('').lower();assert b'PONG' in c.cmd('PING')
   c.cmd('Q',b'> ');result=c.cmd('Y',b'GOODBYE 67');assert b'GOODBYE 67' in result
  finally:a.close();c.close();s.close()
  # Missing and empty optional text are valid; malformed UTF-8 is rejected.
  for value in (None,b''):
   if value is None:info.unlink()
   else:info.write_bytes(value)
   s=Server(binary,board,members=users,options=options);s.close()
  info.write_bytes(b'\xff')
  result=subprocess.run([binary,*options,'0',str(board),str(users)],capture_output=True,timeout=10)
  assert result.returncode!=0
 print('PASS stage67 Telnet reading, shared buffer, role menus, scope, questions, timezone, goodbye')
if __name__=='__main__':run(*sys.argv[1:])
