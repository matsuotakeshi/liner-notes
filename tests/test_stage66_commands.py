"""RTBBS keys over actual Telnet, contextual dispatch and mutation safety."""
from pathlib import Path
import subprocess,sys,tempfile,time
from test_board import Server,post,read
from test_runtime import Client as Raw
from test_members import auth,PASSWORD
from test_mail import send
from test_telnet import Client

def run(binary,vaultctl):
 binary=str(Path(binary).resolve());vaultctl=str(Path(vaultctl).resolve())
 with tempfile.TemporaryDirectory(prefix='commands66-') as tmp:
  p=Path(tmp);admins=p/'admins';admins.write_text('1\n')
  subprocess.run([vaultctl,'init',str(p/'vault')],check=True,capture_output=True)
  s=Server(binary,p/'notes',members=p/'members',language='en');raw=Raw(s.port)
  try:
   auth(raw,'alice',True);post(raw,'First','first body');post(raw,'Second','second body');post(raw,'Child','child body',1)
   send(raw,'alice','Mail one','mail body');raw.close();raw=Raw(s.port);auth(raw,'bob',True)
  finally:raw.close();s.close()
  s=Server(binary,p/'notes',members=p/'members',language='en',menu=True,telnet=True,admins=admins,vault=p/'vault')
  c=Client(s.port);g=Client(s.port)
  try:
   assert b'RTBBS-style' in c.until();g.until()
   # Main A is registration, M mail, F vault, not the old alias meanings.
   assert b'Username' in g.cmd('a',b'> ');g.cmd('/cancel')
   assert b'Please log in' in g.cmd('m')
   assert b'Please log in' in g.cmd('f')
   c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD)
   assert b'Connections' in c.cmd('w') or b'alice' in c.cmd('w')
   out=c.cmd('l');assert b'UTC' in out,out
   assert b'gateway' in c.cmd('x')
   assert b'stage71' in c.cmd('v')
   c.cmd('\\',b'> ');c.cmd('Mixed Case Memo');assert b'Mixed Case Memo' in c.cmd('W')
   assert b'INSTALL' in c.cmd('i');c.cmd('h',b'> ');c.cmd('Alice Mixed');assert b'Alice Mixed' in c.cmd('PROFILE')
   c.cmd('1',b'> ');c.cmd('beta');c.cmd('4',b'> ');out=c.cmd('0');assert b'OFF' in out,out
   c.cmd('0');assert b'PROFILE' in c.cmd('p');c.cmd('p',b'> ');assert b'Alice Mixed' in c.cmd('alice');c.cmd('0')
   assert b'Vault' in c.cmd('f');assert b'1' in c.cmd('b',b'> ');c.cmd('0');c.cmd('i');assert b'MAIN ' in c.cmd('MENU')
   assert b'Gather' in c.cmd('g',b'Gather > ');c.cmd('/CANCEL')
   c.cmd('c',b'Chat> ');c.cmd('//')
   c.cmd('m',b'0 exit)> ');c.cmd('n',b'> ');out=c.cmd('2000-01-01',b'0 exit)> ');assert b'Mail one' in out
   c.cmd('E',b'> ');c.cmd('alice',b'> ');c.cmd('N',b'Body > ');c.cmd('/CANCEL',b'0 exit)> ')
   c.cmd('K 1',b'> ');c.cmd('N',b'0 exit)> ');assert b'mail body' in c.cmd('1',b'0 exit)> ')
   assert b'MAIN ' in c.cmd('MENU')
   c.cmd('b');c.cmd('1');assert b'BOARD' in c.cmd('?')
   c.cmd('e',b'> ');c.cmd('New title',b'Body > ');c.cmd('New body',b'Body > ')
   assert b'W: write' in c.cmd('//',b'> ')
   # No commit before W, and E returns to editing.
   c.cmd('e',b'Body > ');c.cmd('/PREVIEW',b'Body > ');c.cmd('//',b'> ')
   assert b'Saved article #4' in c.cmd('w')
   c.cmd('0');c.cmd('1');assert b'Article #3' in c.cmd('a')
   assert b'Article #1' in c.cmd('d')
   assert b'Article #1' in c.cmd('l');c.cmd('STOP')
   assert b'Article #2' in c.cmd('');c.cmd('STOP')
   assert b'Article #1' in c.cmd('p');c.cmd('STOP')
   c.cmd('k',b'> ');c.cmd('n');assert b'first body' in c.cmd('READ 1')
   c.cmd('e',b'> ');c.cmd('2',b'> ');c.cmd('Renamed title')
   assert b'Renamed title' in c.cmd('READ 1')
   c.cmd('0');c.cmd('f',b'> ');assert b'Renamed title' in c.cmd('alice')
   c.cmd('n',b'> ');assert b'Renamed title' in c.cmd('2000-01-01')
   c.cmd('READ 4');c.cmd('k',b'> ');assert b'withdraw' in c.cmd('y').lower()
   # Named commands retain their permissions; no privilege supplied by a menu.
   g.cmd('LOGIN bob',b'> ');g.cmd(PASSWORD);g.cmd('READ 1');assert b'administrator' in g.cmd('e').lower()
   g.cmd('k',b'> ');g.cmd('y');assert b'first body' in g.cmd('READ 1')
   c.cmd('MENU');c.cmd('SEARCH Renamed');assert b'READ ' in c.cmd('1');assert b'Article #1' in c.cmd('l');c.cmd('STOP')
   c.cmd('MENU');c.cmd('LANG JA');assert '個人設定'.encode() in c.cmd('?')
   assert b'INSTALL' in c.cmd('i');c.cmd('h',b'> ');c.cmd('Case Preserved');c.cmd('0')
   c.cmd('b');c.cmd('1');assert '日付'.encode() in c.cmd('n',b'> ');c.cmd('/cancel')
   c.cmd('MENU');c.cmd('LANG EN');c.cmd('O',b'> ');c.cmd('Y',b'Goodbye');assert not c.sock.recv(1)
  finally:c.close();g.close();s.close()
  # Data persisted, including the correctly cancelled destructive action.
  s=Server(binary,p/'notes',members=p/'members');raw=Raw(s.port)
  try:auth(raw,'alice');assert read(raw,1)[1]=='Renamed title'
  finally:raw.close();s.close()
 print('PASS stage66: native Telnet RTBBS main/board/read/mail/vault/settings/profile keys, confirmation, authorization, persistence')
if __name__=='__main__':run(*sys.argv[1:])
