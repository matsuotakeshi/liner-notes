"""Telnet menu path for X/Y transfer connectors and trusted receipt protocol."""
import pathlib,sys,tempfile,subprocess,shlex,hashlib
from test_board import Server
from test_runtime import Client
from test_telnet import Client as Telnet
from test_members import auth,PASSWORD
from test_zmodem_menu import transfer
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from transfer_connectors import LineConnector

def run(binary,ctl,receiver,sender):
 binary,ctl,receiver,sender=[str(pathlib.Path(x).resolve()) for x in (binary,ctl,receiver,sender)]
 root=pathlib.Path(__file__).resolve().parents[1]
 with tempfile.TemporaryDirectory(prefix='legacy-menu61-') as d:
  p=pathlib.Path(d);v=p/'vault';m=p/'members';b=p/'board';spool=p/'spool';spool.mkdir();origin=p/'origin';origin.mkdir()
  subprocess.run([ctl,'init',str(v)],check=True,capture_output=True)
  helper=p/'helper';helper.write_text('#!/bin/sh\nexec '+shlex.join([sys.executable,str(root/'scripts/zmodem_host.py'),'--receiver',receiver,'--sender',sender,'--spool-root',str(spool),'--timeout','15'])+' -- "$@"\n');helper.chmod(0o700)
  s=Server(binary,b,members=m,vault=v);c=Client(s.port);auth(c,'admin',True);c.close();s.close();admins=p/'admins';admins.write_text('1\n')
  s=Server(binary,b,members=m,vault=v,admins=admins,zhelper=helper,menu=True,telnet=True,language='en');c=Telnet(s.port);api=LineConnector('127.0.0.1',s.port,'admin',PASSWORD)
  try:
   c.until();c.cmd('LOGIN admin',b'> ');c.cmd(PASSWORD)
   for ident,mode in enumerate(('XMODEM','YMODEM'),1):
    name='xy'+str(ident)+'.bin';payload=bytes(range(256))*19+b'last\x1a';(origin/name).write_bytes(payload);c.cmd('protocol '+mode.lower())
    args='-|'+name+'|'+str(len(payload))+'|description' if mode=='XMODEM' else '-|description'
    c.cmd('UPLOAD '+args,b'selected protocol.\r\n')
    code,out,err=transfer(c,sender,['--xmodem' if mode=='XMODEM' else '--ymodem','--',name],origin)
    assert code==0 and b'transfer completed' in out,(code,out,err)
    assert api.info(ident)==(len(payload),hashlib.sha256(payload).hexdigest())
    dest=p/('target'+str(ident));dest.mkdir();c.cmd('DOWNLOAD '+str(ident),b'selected protocol.\r\n')
    args=['--xmodem','--with-crc','--',name] if mode=='XMODEM' else ['--ymodem','--protect']
    code,out,err=transfer(c,receiver,args,dest);assert code==0 and b'transfer completed' in out,(code,out,err)
    got=(dest/name).read_bytes();assert got[:len(payload)]==payload
    api.protocol.send('FILEHISTORY '+str(ident));lines=[]
    while True:
     row=api.protocol.line();lines.append(row)
     if row.startswith(b'END NEXT '):break
    assert any(row.startswith(b'DOWNLOADROW ') and row.endswith(mode.encode()) for row in lines),lines
   result=c.cmd('BATCH MATCH xy?.bin');assert b'Selected 2' in result,result
   dest=p/'batch';dest.mkdir();c.cmd('BATCH SEND',b'selected protocol.\r\n')
   code,out,err=transfer(c,receiver,['--ymodem','--protect'],dest);assert code==0 and b'transfer completed' in out,(code,out,err)
   assert len(list(dest.iterdir()))==2
   for ident,(mode,code) in enumerate((('YMODEM-G','g'),('MLINK','m'),('TEXT','t')),3):
    name='legacy'+str(ident)+'.txt';payload=b'Mixed text\r\nAll lines\r\n' if code=='t' else bytes(range(256))*9+b'end';(origin/name).write_bytes(payload);c.cmd('PROTOCOL '+mode)
    args='-|'+name+'|0|description' if code=='t' else '-|description'
    c.cmd('UPLOAD '+args,b'selected protocol.\r\n')
    peerargs=[str(root/'tests/legacy_wire_peer.py'),code,'send',name]
    status,out,err=transfer(c,sys.executable,peerargs,origin,base_options=[]);assert status==0 and b'transfer completed' in out,(status,out,err)
    assert api.info(ident)==(len(payload),hashlib.sha256(payload).hexdigest())
    dest=p/('target'+str(ident));dest.mkdir();c.cmd('DOWNLOAD '+str(ident),b'selected protocol.\r\n');peerargs=[str(root/'tests/legacy_wire_peer.py'),code,'receive',name]
    status,out,err=transfer(c,sys.executable,peerargs,dest,base_options=[]);assert status==0 and b'transfer completed' in out,(status,out,err)
    assert (dest/name).read_bytes()==payload
   assert b'PONG' in c.cmd('PING')
  finally:c.close();api.close();s.close()
 print('PASS Telnet X/Y/G/M-LINK/TEXT upload/download, wildcard batch and receipt protocol')
if __name__=='__main__':run(*sys.argv[1:])
