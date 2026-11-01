"""CRLF must not cancel F; paged first article can enter continuous mode."""
from pathlib import Path
import sys,tempfile,time
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_mail import send
from test_telnet import Client as Telnet

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='scan65-') as tmp:
  p=Path(tmp);s=Server(binary,p/'notes',members=p/'members',language='en');a=Client(s.port)
  try:
   auth(a,'alice',True)
   for i in range(25):post(a,'continuous '+str(i),'\n'.join('line '+str(n) for n in range(25))+f'\nLAST-{i}')
   for i in range(3):send(a,'alice','mail '+str(i),'MAIL-LAST-'+str(i))
  finally:a.close();s.close()
  s=Server(binary,p/'notes',members=p/'members',menu=True,telnet=True,language='en');a=Telnet(s.port)
  try:
   a.until();a.cmd('LOGIN alice',b'> ');a.cmd(PASSWORD);a.cmd('PAGER ON');a.cmd('PAGER SIZE 80 12')
   a.cmd('SCAN ASC ALL * *',b'[Enter/Q/OFF]> ');a.send(b'F\r\n');out=a.until();assert b'LAST-24' in out,out[-500:]
   # Stop before output is queued for every article, even with a read in flight.
   a.cmd('SCAN ASC ALL * *',b'[Enter/Q/OFF]> ');a.send(b'F\r\n\x03');out=a.until();assert b'LAST-24' not in out,out[-500:]
   assert b'PONG' in a.cmd('PING')
   a.cmd('PAGER OFF');a.cmd('MAIL SCAN',b'0 exit)> ');a.send(b'F\r\n');out=a.until(b'0 exit)> ');assert b'MAIL-LAST-2' in out,out
   a.cmd('MAIL EXIT');assert b'PONG' in a.cmd('PING')
  finally:a.close();s.close()
 print('stage65 Telnet F/CRLF, pager entry, Ctrl-C cancellation and mail continuous reading passed')
if __name__=='__main__':run(sys.argv[1])
