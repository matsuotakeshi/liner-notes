"""Real Telnet: saved description, receiver checks, progress timeout, exact payload."""
from pathlib import Path
import sys,tempfile,subprocess,shlex,hashlib,time,os
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet
from test_zmodem_menu import transfer
from test_zupload_retry import cut_sender
from transfer_connectors import LineConnector

def run(binary,ctl,receiver,sender,payload_file=None):
 binary,ctl,receiver,sender=map(lambda x:str(Path(x).resolve()),(binary,ctl,receiver,sender));root=Path(__file__).resolve().parents[1]
 with tempfile.TemporaryDirectory(prefix='zresume-default-') as tmp:
  p=Path(tmp);spool=p/'spool';source=p/'source';spool.mkdir();source.mkdir();vault=p/'vault';notes=p/'notes';users=p/'users'
  subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
  subprocess.run([ctl,'board-set',str(vault),'-','all','member'],check=True,capture_output=True)
  subprocess.run([ctl,'board-set',str(vault),'apps','member','member'],check=True,capture_output=True)
  # Copy the valid engine receipt so we can test missing receipt without touching the installed one.
  receipt=p/'receipt.json';receipt.write_bytes(Path(receiver+'.liner-notes-resume.json').read_bytes())
  helper=p/'helper';helper.write_text('#!/bin/sh\nexport LINER_NOTES_RESUME_RECEIPT='+shlex.quote(str(receipt))+'\nexec '+shlex.join([sys.executable,str(root/'scripts/zmodem_host.py'),'--receiver',receiver,'--sender',sender,'--spool-root',str(spool),'--timeout','3'])+' -- "$@"\n');helper.chmod(0o700)
  s=Server(binary,notes,members=users);a=Client(s.port);auth(a,'alice',True);a.close();s.close()
  s=Server(binary,notes,members=users,vault=vault,zhelper=helper,menu=True,telnet=True,language='en');c=Telnet(s.port);api=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
  try:
   s.expected_errors=['ZMODEM failure: TIMEOUT','ZMODEM failure: MISMATCH','ZMODEM failure: RECEIVER','ZMODEM failure: NO_PENDING']
   if payload_file:c.sock.settimeout(1800)
   c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD);c.cmd('PAGER OFF');c.cmd('FILEBOARD apps')
   name=Path(payload_file).name if payload_file else 'payload with spaces.bin'
   original=Path(payload_file).read_bytes() if payload_file else bytes(range(256))*8192
   (source/name).write_bytes(original)
   c.cmd('ZUPLOADIN apps|saved description',b'1 GiB.\r\n');cut_sender(c,sender,source,name)
   assert b'Transfer timed out' in c.until();partial=next(spool.glob('pending-*/data/*'));prefix=partial.read_bytes();assert 0<len(prefix)<len(original)
   c.cmd('ZRESUME apps|wrong description',b'1 GiB.\r\n');assert b'description or single/batch mode differs' in c.until();assert partial.read_bytes()==prefix
   saved_receipt=receipt.read_bytes();receipt.unlink()
   c.cmd('ZRESUME',b'1 GiB.\r\n');assert b'verification is missing or stale' in c.until();assert partial.read_bytes()==prefix
   receipt.write_bytes(saved_receipt)
   c.cmd('ZRESUME',b'1 GiB.\r\n');started=time.monotonic()
   code,out,err=transfer(c,sender,['--',name],source,deadline_seconds=900,send_rate=(int(os.environ.get("LINER_TEST_SEND_RATE","524288")) or None) if payload_file else 128*1024)
   elapsed=time.monotonic()-started;assert code==0 and b'published' in out,(code,out,err)
   assert b'Registering received data:' in out,out
   assert api.info(1)==(len(original),hashlib.sha256(original).hexdigest());assert elapsed>3
   if payload_file:assert elapsed>120,elapsed
   assert not list(spool.iterdir())
   c.cmd('ZRESUME',b'1 GiB.\r\n');assert b'No interrupted transfer in this board' in c.until()
   print(f'PASS: bare ZRESUME reuses current board and saved description; missing receipt/mismatch/no partial diagnosed; {len(original)} bytes SHA256 {hashlib.sha256(original).hexdigest()} resumed in {elapsed:.1f}s with 3s idle timeout; original name preserved')
  finally:c.close();api.close();s.close()
if __name__=='__main__':run(*sys.argv[1:])
