"""Same Telnet connection: interrupted reception then a fresh ZUPLOAD."""
from pathlib import Path
import sys,tempfile,subprocess,shlex,hashlib,socket,select,time
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet
from test_zmodem_menu import transfer
from transfer_connectors import LineConnector

def cut_sender(c,sender,source,name):
 left,right=socket.socketpair();proc=subprocess.Popen([sender,'--binary','--quiet','--escape','--syslog=off','--',name],cwd=source,stdin=right,stdout=right,stderr=subprocess.DEVNULL);right.close();left.setblocking(False)
 pending=bytearray(c.buf);c.buf=b'';sent=0;deadline=time.monotonic()+15
 try:
  while sent<700000:
   assert time.monotonic()<deadline
   reads,writes,_=select.select([left,c.sock],[left] if pending else [],[],.02)
   if left in writes:
    try:n=left.send(pending);del pending[:n]
    except BlockingIOError:pass
   if left in reads:
    data=left.recv(min(8192,700000-sent));assert data;c.send(data.replace(b'\xff',b'\xff\xff'));sent+=len(data)
   if c.sock in reads:c.receive();pending.extend(c.buf);c.buf=b''
  # Kill only the client transfer engine; retain this BBS/Telnet connection.
  proc.kill();proc.wait(timeout=5);c.buf=bytes(pending)+c.buf
 finally:
  if proc.poll() is None:proc.kill();proc.wait()
  left.close()

def run(binary,ctl,receiver,sender,mode='fixed'):
 binary,ctl,receiver,sender=map(lambda x:str(Path(x).resolve()),(binary,ctl,receiver,sender));root=Path(__file__).resolve().parents[1]
 with tempfile.TemporaryDirectory(prefix='zupload-retry-') as tmp:
  p=Path(tmp);spool=p/'spool';source=p/'source';spool.mkdir();source.mkdir();vault=p/'vault';notes=p/'notes';users=p/'users'
  subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
  count=p/'starts';rz=p/'receiver';rz.write_text('#!/bin/sh\ncase "$1" in --zmodem) echo start >> '+shlex.quote(str(count))+' ;; esac\nexec '+shlex.quote(receiver)+' "$@"\n');rz.chmod(0o700)
  helper=p/'helper';helper.write_text('#!/bin/sh\nexec '+shlex.join([sys.executable,str(root/'scripts/zmodem_host.py'),'--receiver',str(rz),'--sender',sender,'--spool-root',str(spool),'--timeout','5'])+' -- "$@"\n');helper.chmod(0o700)
  s=Server(binary,notes,members=users);a=Client(s.port);auth(a,'alice',True);a.close();s.close()
  s=Server(binary,notes,members=users,vault=vault,zhelper=helper,menu=True,telnet=True,language='en');c=Telnet(s.port);api=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
  try:
   s.expected_errors=['ZMODEM failure: TIMEOUT']
   c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD);c.cmd('PAGER OFF')
   original=bytes(range(256))*8192;(source/'partial.bin').write_bytes(original)
   c.cmd('ZUPLOAD retry',b'1 GiB.\r\n');cut_sender(c,sender,source,'partial.bin')
   assert b'Transfer timed out' in c.until();assert b'PONG' in c.cmd('PING')
   partial=next(spool.glob('pending-*/data/partial.bin'));data=partial.read_bytes();assert 0<len(data)<len(original)
   assert count.read_text().splitlines()==['start']
   c.cmd('ZUPLOAD retry',b'1 GiB.\r\n')
   if mode=='blocked':
    assert b'failed or cancelled' in c.until();assert count.read_text().splitlines()==['start'];assert partial.read_bytes()==data
    print('REPRODUCED: second ZUPLOAD rejected before lrz starts; same connection remains usable');return
   payload=b'fresh upload after interruption';(source/'fresh.bin').write_bytes(payload)
   code,out,err=transfer(c,sender,['--','fresh.bin'],source);assert code==0 and b'published' in out,(code,out,err)
   assert api.info(1)==(len(payload),hashlib.sha256(payload).hexdigest())
   saved=list(spool.glob('retained-*/data/partial.bin'));assert len(saved)==1 and saved[0].read_bytes()==data
   assert not list(spool.glob('pending-*'))
   assert count.read_text().splitlines()==['start','start']
   # A third fresh transfer also succeeds; no stale active-transfer accounting.
   c.cmd('ZUPLOAD next',b'1 GiB.\r\n');code,out,err=transfer(c,sender,['--','fresh.bin'],source)
   assert code==0 and b'published' in out,(code,out,err);assert api.info(2)==api.info(1)
   print('PASS: real timeout retains partial; next two ZUPLOADs on same Telnet connection start lrz and publish exact payload; original partial preserved')
  finally:c.close();api.close();s.close()
if __name__=='__main__':run(*sys.argv[1:])
