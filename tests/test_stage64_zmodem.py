"""Stage64: 16 private attachments transferred in one ZMODEM session."""
from test_zmodem_workflows import spool_contents
import hashlib,pathlib,shlex,subprocess,sys,tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet
from test_talk import command
from test_zmodem_menu import transfer
from transfer_connectors import MailConnector

class MailCommands:
    def __init__(self,connector):self.connector=connector
    def send(self,data):self.connector.protocol.wire.send_bytes(data)
    def line(self):return self.connector.protocol.line()
    def close(self):self.connector.close()

def run(binary,ctl,receiver,sender):
    binary,receiver,sender=[str(pathlib.Path(x).resolve()) for x in (binary,receiver,sender)]
    root=pathlib.Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='mail-zmodem-') as d:
        p=pathlib.Path(d);data=p/'bbs';members=p/'members';spool=p/'spool';source=p/'source';target=p/'target'
        for path in (spool,source,target):path.mkdir()
        helper=p/'helper';helper.write_text('#!/bin/sh\nexec '+shlex.join([sys.executable,str(root/'scripts/zmodem_host.py'),'--receiver',receiver,'--sender',sender,'--spool-root',str(spool),'--timeout','5'])+' -- "$@"\n');helper.chmod(0o700)
        s=Server(binary,data,members=members);clients=[Client(s.port) for _ in range(3)]
        try:
            for c,n in zip(clients,('alice','bob','carol')):auth(c,n,True)
        finally:
            for c in clients:c.close()
            s.close()
        admins=p/'admins';admins.write_text('3\n')
        s=Server(binary,data,members=members,admins=admins,zhelper=helper,menu=True,telnet=True,language='en',options=['--disable','vault'])
        c=Telnet(s.port);a=Telnet(s.port);sysop=Telnet(s.port);writer=MailConnector('127.0.0.1',s.port,'bob',PASSWORD);raw=MailCommands(writer);api=MailConnector('127.0.0.1',s.port,'alice',PASSWORD)
        try:
            for x,n in [(c,'bob'),(a,'alice'),(sysop,'carol')]:x.until();x.cmd('LOGIN '+n,b'> ');x.cmd(PASSWORD);x.cmd('PAGER OFF')
            payload=bytes(range(256))*81;name='mail-private.bin';(source/name).write_bytes(payload)
            c.cmd('ZMAILUPLOAD private',b'1 GiB.\r\n');code,out,err=transfer(c,sender,['--',name],source)
            assert code==0 and b'Private mail file #1 ready' in out,(code,out,err)
            extras={}
            for ident in range(2,17):
                content=('private-attachment-'+str(ident)).encode();new=writer.begin(len(content),hashlib.sha256(content).hexdigest(),name,'batch');assert new==ident
                writer.write(new,0,content);writer.finish(new);extras[str(ident)+'-'+name]=content
            command(raw,'MAIL SEND alice attached',b'MAIL EDIT ')
            for ident in range(1,17):command(raw,'/ATTACH '+str(ident),('MAIL ATTACHMENT '+str(ident)).encode())
            raw.send(b'private body\n');command(raw,'.',b'MAIL SENT 1')
            assert api.info(1)==(len(payload),hashlib.sha256(payload).hexdigest())
            a.cmd('ZMAILDOWNLOAD 1',b'1 GiB.\r\n');code,out,err=transfer(a,receiver,['--restricted','--protect'],target)
            assert code==0 and b'sent (' in out,(code,out,err);assert (target/name).read_bytes()==payload
            sysop.cmd('ZMAILDOWNLOAD 1',b'1 GiB.\r\n');assert b'failed or cancelled' in sysop.until()
            batch=p/'batch';batch.mkdir()
            a.cmd('ZMAILDOWNLOADALL 1',b'16 GiB total.\r\n');code,out,err=transfer(a,receiver,['--restricted','--protect'],batch)
            assert code==0 and b'Batch sent: 16 files' in out,(code,out,err)
            assert (batch/name).read_bytes()==payload
            for filename,content in extras.items():assert (batch/filename).read_bytes()==content
            assert len(list(batch.iterdir()))==16
            assert b'unavailable' in sysop.cmd('ZMAILDOWNLOADALL 1')
            assert not spool_contents(spool)
            # A board/member epoch change interrupts a private transfer too.
            c.cmd('ZMAILUPLOAD',b'1 GiB.\r\n');sysop.cmd('LEVEL 2 1')
            import time
            deadline=time.monotonic()+5
            while time.monotonic()<deadline:
                if not c.sock.recv(4096):break
            else:raise AssertionError('private transfer remained active after revocation')
        finally:c.close();a.close();sysop.close();raw.close();api.close();s.close()
    print('PASS stage64: real 16-attachment ZMODEM batch, duplicate filenames, SYSOP batch denial, single-file upload/download, receipt, SHA256, public-vault independence, SYSOP denial and revocation')
if __name__=='__main__':run(*sys.argv[1:])
