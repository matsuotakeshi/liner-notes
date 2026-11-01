"""Administrator controls act on guest/transfer sessions and stop child engines."""
import pathlib,sys,tempfile,subprocess,socket
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet,disconnected
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from transfer_connectors import LineConnector

def run(binary,ctl):
 binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
 with tempfile.TemporaryDirectory(prefix='system-control-') as name:
  p=pathlib.Path(name);data=p/'data';users=p/'users';admins=p/'admins';vault=p/'vault';helper=p/'helper'
  helper.write_text('#!/bin/sh\nexec sleep 30\n');helper.chmod(0o700)
  subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
  s=Server(binary,data,members=users);a,b=Client(s.port),Client(s.port)
  try:auth(a,'alice',True);auth(b,'bob',True)
  finally:a.close();b.close();s.close()
  admins.write_text('1\n');s=Server(binary,data,members=users,admins=admins,vault=vault,zhelper=helper,menu=True,telnet=True,language='en')
  api=LineConnector('127.0.0.1',s.port,'alice',PASSWORD);c=Telnet(s.port);guest=Telnet(s.port)
  def roster():
   api.protocol.request('CONNECTIONS',lambda x:x==b'CONNECTIONS');rows=[]
   while True:
    row=api.protocol.line()
    if row==b'END CONNECTIONS':return rows
    if row.startswith(b'CONNECTION '):rows.append(row.split())
  try:
   c.until();guest.until();c.cmd('LOGIN bob',b'> ');c.cmd(PASSWORD);c.cmd('ZUPLOAD',b'1 GiB.\r\n')
   rows=roster();target=next(int(x[1]) for x in rows if x[2:4]==[b'bob',b'ZMODEM'])
   api.protocol.request('ANNOUNCE no-transfer-injection',lambda x:x==b'ANNOUNCE SENT')
   c.sock.settimeout(.2)
   try:data=c.sock.recv(4096);assert b'no-transfer-injection' not in data
   except socket.timeout:pass
   api.protocol.request(f'DISCONNECT {target} CONFIRM',lambda x:x==f'DISCONNECTED {target}'.encode());disconnected(c)
   guest_target=next(int(x[1]) for x in roster() if x[2]==b'guest')
   api.protocol.request(f'DISCONNECT {guest_target} CONFIRM',lambda x:x==f'DISCONNECTED {guest_target}'.encode());disconnected(guest)
   c.close();c=Telnet(s.port);c.until();c.cmd('LOGIN bob',b'> ');c.cmd(PASSWORD);c.cmd('ZUPLOAD',b'1 GiB.\r\n')
   api.protocol.request('SHUTDOWN 1 end-test',lambda x:x==b'SHUTDOWN SCHEDULED 1');s.proc.wait(timeout=8);assert s.proc.returncode==0;disconnected(c)
  finally:api.close();c.close();guest.close();s.close()
 print('PASS: admin disconnect of ZMODEM and guest connections; announcements never enter transfer wire; scheduled stop reaps active child')
if __name__=='__main__':run(*sys.argv[1:])
