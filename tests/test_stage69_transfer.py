"""Actual Telnet XMODEM checksum/CRC/1K transfers and durable protocol fields."""
from pathlib import Path
import sys,tempfile,subprocess,shlex,hashlib,binascii
from test_board import Server
from test_runtime import Client
from test_telnet import Client as Telnet
from test_members import auth,PASSWORD
from test_zmodem_menu import transfer
from test_stage61_protocols import fixture,exact
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from transfer_connectors import LineConnector
from zmodem_connector import ZmodemProcess

def run(binary,ctl,receiver,sender):
 binary,ctl,receiver,sender=map(lambda x:str(Path(x).resolve()),(binary,ctl,receiver,sender))
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp);v=p/'vault';spool=p/'spool';source=p/'source';spool.mkdir();source.mkdir()
  subprocess.run([ctl,'init',str(v)],check=True,capture_output=True)
  helper=p/'helper';helper.write_text('#!/bin/sh\nexec '+shlex.join([sys.executable,str(ROOT/'scripts/zmodem_host.py'),'--receiver',receiver,'--sender',sender,'--spool-root',str(spool),'--timeout','15'])+' -- "$@"\n');helper.chmod(0o700)
  notes=p/'notes';users=p/'users';s=Server(binary,notes,members=users);c=Client(s.port);auth(c,'alice',True);c.close();s.close()
  s=Server(binary,notes,members=users,vault=v,zhelper=helper,menu=True,telnet=True,language='en');c=Telnet(s.port);api=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
  try:
   c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD);c.cmd('PAGER OFF')
   payload=bytes(range(256))*10+b'final';path=source/'payload.bin';path.write_bytes(payload)
   for ident,(protocol,variant) in enumerate((('XMODEM-SUM','s'),('XMODEM','x'),('XMODEM-1K','k')),1):
    c.cmd('PROTOCOL '+protocol);c.cmd(f'UPLOAD -|payload.bin|{len(payload)}|{protocol}',b'selected protocol.\r\n')
    code,out,err=transfer(c,sender,['--xmodem']+(['--1k'] if variant=='k' else [])+['--','payload.bin'],source)
    assert code==0 and b'transfer completed' in out,(protocol,code,out,err)
    assert api.info(ident)==(len(payload),hashlib.sha256(payload).hexdigest())
    api.protocol.send('FILE '+str(ident));lines=[]
    while True:
     row=api.protocol.line();lines.append(row)
     if row==b'END FILE':break
    assert ('PROTOCOL '+protocol).encode() in lines,lines
    target=p/variant;target.mkdir();c.cmd('DOWNLOAD '+str(ident),b'selected protocol.\r\n')
    code,out,err=transfer(c,receiver,['--xmodem']+([] if variant=='s' else ['--with-crc'])+['--','payload.bin'],target)
    assert code==0 and b'transfer completed' in out,(protocol,code,out,err)
    got=(target/'payload.bin').read_bytes();assert got[:len(payload)]==payload and all(x==26 for x in got[len(payload):])
    # Independent receiver verifies actual wire block size and checksum/CRC.
    engine=ZmodemProcess(receiver,sender,15,protocol='x',x_variant=variant)
    def partner(sock):
     sock.settimeout(8);sock.sendall(b'\x15' if variant=='s' else b'C');data=b'';seq=1
     while True:
      kind=exact(sock,1)
      if kind==b'\x04':sock.sendall(b'\x06');break
      size=128 if kind==b'\x01' else 1024 if kind==b'\x02' else 0;assert size
      if seq==1:assert size==(1024 if variant=='k' else 128),(variant,size)
      number,inverse=exact(sock,2);assert number==seq and number^inverse==255
      block=exact(sock,size);check=exact(sock,1 if variant=='s' else 2)
      assert int.from_bytes(check,'big')==(sum(block)%256 if variant=='s' else binascii.crc_hqx(block,0))
      data+=block;seq=(seq+1)%256;sock.sendall(b'\x06')
     assert data[:len(payload)]==payload
    fixture(lambda fd:engine.send(path,fd,fd,lambda:None),partner)
  finally:c.close();api.close();s.close()
  # New protocol names must replay on restart, including completed receipts.
  from test_stage58 import rows,counts
  s=Server(binary,notes,members=users,vault=v);c=Client(s.port)
  try:
   auth(c,'alice')
   for ident,protocol in enumerate(('XMODEM-SUM','XMODEM','XMODEM-1K'),1):
    assert ('PROTOCOL '+protocol).encode() in rows(c,'FILE '+str(ident),b'END FILE')
    assert counts(c,ident)==1
  finally:c.close();s.close()
 print('PASS Telnet XMODEM SUM/CRC/1K upload/download, exact payload hashes, protocol logs and independent wire validation')

if __name__=='__main__':run(*sys.argv[1:])
