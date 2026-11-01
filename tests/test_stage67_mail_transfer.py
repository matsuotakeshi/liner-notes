"""Actual selected-protocol mail payloads, with public vault disabled."""
import pathlib,sys,tempfile,subprocess,shlex,hashlib
from test_board import Server
from test_runtime import Client
from test_telnet import Client as Telnet
from test_members import auth,PASSWORD
from test_zmodem_menu import transfer
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from transfer_connectors import MailConnector,RemoteError
from test_zmodem_mail import MailCommands
from test_talk import command

def run(binary,receiver,sender):
 binary,receiver,sender=map(lambda x:str(pathlib.Path(x).resolve()),(binary,receiver,sender))
 with tempfile.TemporaryDirectory(prefix='mail67-') as tmp:
  p=pathlib.Path(tmp);spool=p/'spool';spool.mkdir();source=p/'source';source.mkdir()
  helper=p/'helper';helper.write_text('#!/bin/sh\nexec '+shlex.join([sys.executable,str(ROOT/'scripts/zmodem_host.py'),'--receiver',receiver,'--sender',sender,'--spool-root',str(spool),'--timeout','15'])+' -- "$@"\n');helper.chmod(0o700)
  s=Server(binary,p/'notes',members=p/'members')
  for name in ('alice','bob','admin'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close();admins=p/'admins';admins.write_text('3\n')
  s=Server(binary,p/'notes',members=p/'members',admins=admins,zhelper=helper,menu=True,telnet=True,language='en',options=['--disable','vault','--database',str(p/'liner.sqlite')])
  c=Telnet(s.port);a=Telnet(s.port);api=MailConnector('127.0.0.1',s.port,'alice',PASSWORD);raw=MailCommands(api);other=MailConnector('127.0.0.1',s.port,'admin',PASSWORD)
  try:
   for client,name in ((c,'alice'),(a,'bob')):client.until();client.cmd('LOGIN '+name,b'> ');client.cmd(PASSWORD);client.cmd('PAGER OFF')
   for ident,(mode,code) in enumerate((('XMODEM','x'),('YMODEM','y'),('YMODEM-G','g'),('MLINK','m'),('TEXT','t')),1):
    name=f'mail{ident}.bin';payload=b'Mail text\r\nsecond line\r\n' if code=='t' else bytes(range(256))*9+b'end';(source/name).write_bytes(payload)
    c.cmd('PROTOCOL '+mode);a.cmd('PROTOCOL '+mode)
    description=f'{name}|{len(payload) if code=="x" else 0}|mail' if code in ('x','t') else 'mail'
    c.cmd('MAILUPLOAD '+description,b'selected protocol.\r\n')
    if code in ('x','y'):exe,args=sender,['--xmodem' if code=='x' else '--ymodem','--',name];kw={}
    else:exe,args=sys.executable,[str(ROOT/'tests/legacy_wire_peer.py'),code,'send',name];kw={'base_options':[]}
    status,out,err=transfer(c,exe,args,source,**kw);assert status==0 and b'transfer completed' in out,(mode,status,out,err)
    assert api.info(ident)==(len(payload),hashlib.sha256(payload).hexdigest())
    command(raw,'MAIL SEND bob transfer',b'MAIL EDIT ');command(raw,f'/ATTACH {ident}',b'MAIL ATTACHMENT ');raw.send(b'body\n');command(raw,'.',b'MAIL SENT ')
    try:other.info(ident)
    except RemoteError:pass
    else:raise AssertionError('SYSOP read private attachment')
    target=p/f'target{ident}';target.mkdir();a.cmd(f'MAILDOWNLOAD {ident}',b'selected protocol.\r\n')
    if code in ('x','y'):exe,args=receiver,(['--xmodem','--with-crc','--',name] if code=='x' else ['--ymodem','--protect']);kw={}
    else:exe,args=sys.executable,[str(ROOT/'tests/legacy_wire_peer.py'),code,'receive',name];kw={'base_options':[]}
    status,out,err=transfer(a,exe,args,target,**kw);assert status==0,(mode,status,out,err);assert (target/name).read_bytes()[:len(payload)]==payload
   assert b'one file' in c.cmd('MAILUPLOADBATCH description')
   assert b'PONG' in c.cmd('PING')
  finally:c.close();a.close();api.close();other.close();s.close()
 print('PASS stage67 SQLite private mail X/Y/Y-g/M-LINK/TEXT payloads and SYSOP denial')
if __name__=='__main__':run(*sys.argv[1:])
