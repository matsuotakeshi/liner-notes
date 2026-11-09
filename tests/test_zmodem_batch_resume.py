"""Real terminal batch upload and interrupted upload/download wire resumption."""
from test_zmodem_workflows import spool_contents
import hashlib,os,pathlib,select,shlex,socket,subprocess,sys,tempfile,time
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet
from test_zmodem_menu import transfer
from transfer_connectors import LineConnector

def interrupt(c,sender,source,name,cut=1400000):
 left,right=socket.socketpair();proc=subprocess.Popen([sender,'--binary','--quiet','--escape','--syslog=off','--']+([name] if isinstance(name,str) else name),cwd=source,stdin=right,stdout=right,stderr=subprocess.DEVNULL);right.close()
 pending=bytearray(c.buf);c.buf=b'';sent=0;deadline=time.monotonic()+20;left.setblocking(False)
 try:
  while sent<cut:
   assert time.monotonic()<deadline
   reads,writes,_=select.select([left,c.sock],[left] if pending else [],[],.01)
   if left in writes:
    try:n=left.send(pending);del pending[:n]
    except BlockingIOError:pass
   if left in reads:
    data=left.recv(min(8192,cut-sent));assert data;c.send(data.replace(b'\xff',b'\xff\xff'));sent+=len(data)
   if c.sock in reads:c.receive();pending.extend(c.buf);c.buf=b''
  c.close()  # Actual TCP interruption while the runtime helper owns the wire.
 finally:
  left.close();proc.terminate();proc.wait(timeout=5)

def run(binary,ctl,receiver,sender):
 binary,ctl,receiver,sender=[str(pathlib.Path(x).resolve()) for x in (binary,ctl,receiver,sender)]
 root=pathlib.Path(__file__).resolve().parents[1]
 with tempfile.TemporaryDirectory(prefix='z56-') as directory:
  p=pathlib.Path(directory);vault=p/'vault';data=p/'bbs';users=p/'users';spool=p/'spool';spool.mkdir();source=p/'source';source.mkdir();target=p/'target';target.mkdir()
  subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
  helper=p/'helper';helper.write_text('#!/bin/sh\nexec '+shlex.join([sys.executable,str(root/'scripts/zmodem_host.py'),'--receiver',receiver,'--sender',sender,'--spool-root',str(spool),'--timeout','8'])+' -- "$@"\n');helper.chmod(0o700)
  s=Server(binary,data,members=users,vault=vault);a=Client(s.port)
  try:auth(a,'alice',True)
  finally:a.close();s.close()
  # Root policy and inherited board read/registration rights.
  for args in (['-','all','member'],['open','member','member'],['private','l3','l3']):subprocess.run([ctl,'board-set',str(vault)]+args,check=True,capture_output=True)
  s=Server(binary,data,members=users,vault=vault,zhelper=helper,menu=True,telnet=True,language='en');c=Telnet(s.port);api=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
  try:
   c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD)
   payloads={'one.bin':b'one'+bytes(range(256))*256,'two.bin':b'two'+bytes(range(256))*128}
   for name,body in payloads.items():(source/name).write_bytes(body)
   c.cmd('ZUPLOADBATCH open|batch description',b'16 GiB total.\r\n')
   code,out,err=transfer(c,sender,['--']+list(payloads),source);assert code==0 and b'Uploaded 2 files' in out,(code,out,err)
   for ident,body in enumerate(payloads.values(),1):assert api.info(ident)==(len(body),hashlib.sha256(body).hexdigest())
   assert not spool_contents(spool)
   api.protocol.send('FILE 1');rows=[]
   while True:
    row=api.protocol.line();rows.append(row)
    if row==b'END FILE':break
   assert any(b'open' in row for row in rows) and any(b'batch description' in row for row in rows),rows
   # This check fails before receiving or reserving a private-board file.
   c.cmd('ZUPLOADBATCH private|denied',b'16 GiB total.\r\n');assert b'failed or cancelled' in c.until();assert not spool_contents(spool)
   payload=bytes(range(256))*8192;name='resume.bin';(source/name).write_bytes(payload)
   c.cmd('ZUPLOADIN open|resume description',b'1 GiB.\r\n');interrupt(c,sender,source,name)
   time.sleep(1.5)
   pending=list(spool.glob('pending-*/data/resume.bin'));assert len(pending)==1 and 1024<pending[0].stat().st_size<len(payload)
   retained=pending[0].stat().st_size
   c=Telnet(s.port);c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD)
   c.cmd('ZRESUME open|resume description',b'1 GiB.\r\n')
   sent=[0];original_send=c.send
   def counted(data):sent[0]+=len(data);return original_send(data)
   c.send=counted
   code,out,err=transfer(c,sender,['--',name],source);c.send=original_send
   assert code==0 and b'published' in out,(code,out,err)
   assert api.info(3)==(len(payload),hashlib.sha256(payload).hexdigest())
   assert sent[0]<len(payload),(retained,sent[0],len(payload))
   assert not spool_contents(spool)
   # Batch retry includes the completed first file and interrupted second file.
   c.cmd('ZUPLOADBATCH open|resume batch',b'16 GiB total.\r\n');interrupt(c,sender,source,['one.bin',name])
   time.sleep(1.5);assert len(list(spool.glob('pending-*/data/*')))==2
   c=Telnet(s.port);c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD)
   c.cmd('ZUPLOADBATCHRESUME open|resume batch',b'16 GiB total.\r\n')
   code,out,err=transfer(c,sender,['--','one.bin',name],source);assert code==0 and b'Uploaded 2 files' in out,(code,out,err)
   assert api.info(4)==(len(payloads['one.bin']),hashlib.sha256(payloads['one.bin']).hexdigest())
   assert api.info(5)==(len(payload),hashlib.sha256(payload).hexdigest())
   assert not spool_contents(spool)
   # Receiver preserves its own partial and requests resumption from the sender.
   (target/name).write_bytes(payload[:1048576]);c.cmd('ZDOWNLOAD 3',b'1 GiB.\r\n')
   code,out,err=transfer(c,receiver,['--resume','--resume'],target)
   assert code==0 and b'sent (' in out and (target/name).read_bytes()==payload,(code,out,err)
   # Even in resume mode, peer-provided parent paths must not escape staging.
   from test_zmodem import peer
   from test_zmodem_workflows import fails
   from zmodem_connector import ZmodemProcess
   stage=p/'path-check';stage.mkdir();(stage/'safe.bin').write_bytes(b'partial')
   outside=p/'outside.bin';outside.write_bytes(b'outside unchanged')
   engine=ZmodemProcess(receiver,sender,5)
   fails(lambda:peer(sender,['--full-path','--','../outside.bin'],source,
       lambda fd:engine.receive_resume(stage,fd,fd,lambda:None)))
   assert outside.read_bytes()==b'outside unchanged' and (stage/'safe.bin').read_bytes()==b'partial'
   # Different retained bytes restart from zero instead of mixing contents.
   replacement=bytes(range(256))*64;(source/'safe.bin').write_bytes(replacement)
   peer(sender,['--','safe.bin'],source,lambda fd:engine.receive_resume(stage,fd,fd,lambda:None))
   assert (stage/'safe.bin').read_bytes()==replacement
   # Private mail uses its own identity/storage namespace on the same account.
   private=bytes(range(256))*512;(source/'private.bin').write_bytes(private)
   c.cmd('ZMAILUPLOAD private resume',b'1 GiB.\r\n');interrupt(c,sender,source,'private.bin',cut=90000)
   time.sleep(1.5);c=Telnet(s.port);c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD)
   c.cmd('ZMAILRESUME private resume',b'1 GiB.\r\n')
   code,out,err=transfer(c,sender,['--','private.bin'],source);assert code==0 and b'Private mail file #1 ready' in out,(code,out,err)
   from transfer_connectors import MailConnector
   private_api=MailConnector('127.0.0.1',s.port,'alice',PASSWORD)
   try:assert private_api.info(1)==(len(private),hashlib.sha256(private).hexdigest())
   finally:private_api.close()
   assert not spool_contents(spool)
   print(f'PASS: real Telnet batch upload/board ACL/description; TCP cut retained {retained} bytes; resumed upload wire {sent[0]} < full payload {len(payload)}; SHA-256 matches; batch/download/private-mail resume, path rejection and changed-prefix restart')
  finally:c.close();api.close();s.close()
if __name__=='__main__':run(*sys.argv[1:])
