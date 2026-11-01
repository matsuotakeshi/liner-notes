"""Actual Telnet batch: first file succeeds, second skipped; retry only pending."""
import sys,tempfile,subprocess,shlex,hashlib
from pathlib import Path
from test_zmodem_menu import transfer
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet
from transfer_connectors import LineConnector

def run(binary,ctl,receiver,sender):
 binary,ctl,receiver,sender=map(lambda p:str(Path(p).resolve()),(binary,ctl,receiver,sender));root=Path(__file__).resolve().parents[1]
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp);v=p/'vault';spool=p/'spool';target=p/'target';spool.mkdir();target.mkdir();users=p/'users';notes=p/'notes';helper=p/'helper'
  subprocess.run([ctl,'init',str(v)],check=True,capture_output=True)
  helper.write_text('#!/bin/sh\nexec '+shlex.join([sys.executable,str(root/'scripts/zmodem_host.py'),'--receiver',receiver,'--sender',sender,'--spool-root',str(spool),'--timeout','10'])+' -- "$@"\n');helper.chmod(0o700)
  s=Server(binary,notes,members=users,vault=v);c=Client(s.port);auth(c,'alice',True);c.close();s.close()
  s=Server(binary,notes,members=users,vault=v,menu=True,telnet=True,language='en',zhelper=helper);c=Telnet(s.port);api=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
  def count(i):
   api.protocol.send('FILESTATS '+str(i));n=None
   while True:
    row=api.protocol.line()
    if row.startswith(b'DOWNLOADCOUNT '):n=int(row.split()[2])
    if row.startswith(b'END NEXT'):return n
  try:
   for i in (1,2):
    data=('payload'+str(i)).encode();ident=api.begin(len(data),hashlib.sha256(data).hexdigest(),str(i)+'.txt','test');api.write(ident,0,data);api.finish(ident)
   c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD);c.cmd('PAGER OFF');c.cmd('BATCH ADD 1');c.cmd('BATCH ADD 2')
   (target/'2.txt').write_bytes(b'keep this')
   c.cmd('BATCH SEND',b'16 GiB total.\r\n');code,out,err=transfer(c,receiver,['--restricted','--protect'],target)
   status=c.cmd('BATCH LIST');assert b'BATCH FILE 1 DONE' in status and b'BATCH FILE 2 PENDING' in status,status
   assert count(1)==1 and count(2)==0
   assert (target/'1.txt').read_bytes()==b'payload1' and (target/'2.txt').read_bytes()==b'keep this'
   (target/'2.txt').unlink()
   c.cmd('BATCH SEND',b'16 GiB total.\r\n');code,out,err=transfer(c,receiver,['--restricted','--protect'],target)
   assert code==0,(code,out,err)
   assert b'BATCH FILE 2 DONE' in c.cmd('BATCH LIST')
   assert count(1)==1 and count(2)==1
   assert b'BATCH COMPLETE' in c.cmd('BATCH SEND')
  finally:c.close();api.close();s.close()
 print('PASS actual Telnet ZMODEM: partial completion, receipts, retry pending only, no double counting')
if __name__=='__main__':run(*sys.argv[1:])
