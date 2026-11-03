"""Real Telnet PTY, revocation, and classic editor confirmation."""
from pathlib import Path
import sys,tempfile,os,time
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet
from test_stage58 import cmd
from test_stage64_details import until

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='terminal65-') as tmp:
  p=Path(tmp);game=p/'game';game.write_text('#!/bin/sh\necho $$ > game.pid\nprintf "First> "\nIFS= read -r a\nprintf "Second> "\nIFS= read -r b\nprintf "VALUES=%s,%s\\n" "$a" "$b"\n');game.chmod(0o700)
  config=p/'doors';config.write_text(f'1|0|10|{game}|{p}|Interactive test\n')
  s=Server(binary,p/'notes',members=p/'members',language='en');a=Client(s.port)
  try:auth(a,'alice',True);a.close();a=Client(s.port);auth(a,'admin',True)
  finally:a.close();s.close()
  admins=p/'admins';admins.write_text('2\n')
  s=Server(binary,p/'notes',members=p/'members',admins=admins,language='en',menu=True,telnet=True,options=['--doors',str(config)])
  c=Telnet(s.port);a=Telnet(s.port)
  try:
   for wire,name in [(c,'alice'),(a,'admin')]:wire.until();wire.cmd('LOGIN '+name,b'> ');wire.cmd(PASSWORD)
   g=Telnet(s.port)
   try:
    g.until();assert b'Please log in' in g.cmd('POST')
    a.cmd('BOARDSET 1|0|all|all|Public|guest test');a.cmd('BOARDGUEST 1 ON')
    assert b'New note title' in g.cmd('POST',b'> ');g.cmd('Guest title',b'Body > ');g.cmd('/CANCEL')
   finally:g.close()
   assert b'Interactive test' in c.cmd('GAMES')
   c.cmd('DOOR 1',b'First> ');c.send(b'one\r\n');c.until(b'Second> ');c.send(b'two\r\n');out=c.until();assert b'VALUES=one,two' in out,out
   c.cmd('DOOR 1',b'First> ');pid=int((p/'game.pid').read_text())
   assert b'DOOR' in a.cmd('CONNECTIONS')
   a.cmd('LEVEL 1 1');c.sock.settimeout(5)
   while c.sock.recv(8192):pass
   time.sleep(.5) # bounded Door hangup grace before SIGKILL
   try:os.kill(pid,0)
   except ProcessLookupError:pass
   else:raise AssertionError('Door survived account revocation')
  finally:c.close();a.close();s.close()
  s=Server(binary,p/'notes',members=p/'members',language='en');a=Client(s.port)
  try:
   auth(a,'alice');cmd(a,'EDITOR LINE');assert cmd(a,'POST receive').startswith(b'EDIT')
   a.send(b'A\nkeep\n//\n');assert a.line()==b'DRAFT LINE INSERTED'
   a.send(b'U\n');until(a,b'(N appends) > ');a.send(b'N\n');assert b'Receiving text' in a.line()
   a.send(b'append\n');until(a,b'Text received. Continue editing.\r\n')
   a.send(b'U\n');until(a,b'(N appends) > ');a.send(b'Y\n');assert b'Receiving text' in a.line()
   a.send(b'replacement\n');until(a,b'Text received. Continue editing.\r\n')
   a.send(b'0\n');until(a,b'> ');assert cmd(a,'W').startswith(b'SAVED 1');assert read(a,1)[2]=='replacement\r\n'
  finally:a.close();s.close()
 print('stage65 Telnet PTY CRLF, connections, revocation cleanup, U clear/append confirmation passed')
if __name__=='__main__':run(sys.argv[1])
