"""Runtime-owned ZMODEM helper: wire on stdio, authenticated vault RPC on FD 3.
No password, journal path, or network listener is provided to this process.
"""
import time
import argparse,os,pathlib,signal,socket,sys,hashlib,tempfile
from transfer_connectors import Protocol,LineConnector,MailConnector,MailProtocol,download_begin,download_read,download_done,download_abort
from zmodem_connector import ZmodemProcess,TransferFailure,safe_name,MAX_BYTES,MAX_BATCH_BYTES,MAX_BATCH_FILES
from legacy_transfer import LegacyEngine
from vault_zmodem import receive,send,runtime_receive,PublicationFailure

class LocalProtocol(Protocol):
    def __init__(self):
        self.sock=socket.socket(fileno=3);self.sock.settimeout(10)
        self.buf=bytearray();self.telnet=False;self.binary_stream=False
    def send(self,text):
        if '\n' in text or '\r' in text or len(text.encode())>1024:raise ValueError('Invalid control request')
        self.sock.sendall(text.encode()+b'\n')
    def receive(self):
        data=self.sock.recv(4096)
        if not data:raise ValueError('Runtime disconnected')
        self.buf.extend(data)
        if len(self.buf)>32768:raise ValueError('Oversized reply')

def batch_path(directory,name,ident):
    """Keep duplicate output names unique and within the connector byte limit."""
    name=safe_name(name);root=pathlib.Path(directory);path=root/name
    attempt=0
    while path.exists():
        prefix=str(ident)+'-' if not attempt else str(ident)+'-'+str(attempt)+'-'
        tail=name.encode('utf-8')[:100-len(prefix)].decode('utf-8',errors='ignore')
        path=root/safe_name(prefix+tail);attempt+=1
    return path

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--receiver',required=True);p.add_argument('--sender',required=True)
    p.add_argument('--spool-root',required=True);p.add_argument('--timeout',type=int,default=120)
    p.add_argument('action');p.add_argument('ident',type=int)
    p.add_argument('user');p.add_argument('text');p.add_argument('port',type=int)
    args=p.parse_args();os.umask(0o077)
    protocol='z'
    if len(args.action)>2 and args.action[0] in 'xygmtsk' and args.action[1]=='-':protocol,args.action=args.action[0],args.action[2:]
    if args.action not in ('receive','receive-in','receive-mail','receive-mail-many','receive-mail-many-resume','receive-many','receive-resume','receive-many-resume','receive-mail-resume','send-mail','send','batch','batch-mail'):p.error('Invalid action')
    if protocol!='z' and args.action not in ('receive-in','receive-many','send','batch','receive-mail','receive-mail-many','send-mail','batch-mail'):p.error('Unsupported protocol action')
    c=(MailConnector if args.action in ('receive-mail','receive-mail-many','receive-mail-many-resume','receive-mail-resume','send-mail','batch-mail') else LineConnector).__new__(MailConnector if args.action in ('receive-mail','receive-mail-many','receive-mail-many-resume','receive-mail-resume','send-mail','batch-mail') else LineConnector);c.protocol=LocalProtocol()
    if args.action in ('receive-mail','receive-mail-many','receive-mail-many-resume','receive-mail-resume','send-mail','batch-mail'):c.protocol=MailProtocol(c.protocol)
    def release_downloads():
        for active_id in list(getattr(c,"_downloads",{})):download_abort(c,active_id)
    def interrupted(*_):raise TransferFailure('Runtime stopped transfer')
    for sig in (signal.SIGTERM,signal.SIGHUP,signal.SIGINT):signal.signal(sig,interrupted)
    last_progress=[-1.0]
    def registration_progress(done,total):
        now=time.monotonic()
        if done not in (0,total) and now-last_progress[0]<1:return
        last_progress[0]=now
        c.protocol.send(f'PROGRESS {done} {total}')
    c.registration_progress=registration_progress
    try:
        if not 1<=args.timeout<=3600:raise TransferFailure('Invalid timeout')
        c.protocol.send(f'IDLE {args.timeout}')
        filename=None;expected_size=None
        if protocol in ('x','t','s','k') and args.action in ('receive-in','receive-mail'):
            parts=(args.text if args.action=='receive-in' else '-|'+args.text).split('|',3)
            if len(parts)!=4 or not parts[2].isdigit():raise TransferFailure('Use board|filename|bytes|description')
            category,filename,expected_size,description=parts;safe_name(filename);expected_size=int(expected_size)
            if expected_size>MAX_BYTES:raise TransferFailure('File too large')
            args.text=category+'|'+description if args.action=='receive-in' else description
        if protocol in ('z','x','y','s','k'):engine=ZmodemProcess(args.receiver,args.sender,args.timeout,process_group=False,protocol='x' if protocol in ('s','k') else protocol,filename=filename,expected_size=expected_size,x_variant=protocol)
        else:engine=LegacyEngine(protocol,args.timeout,filename,expected_size,args.receiver,args.sender)
        if protocol!='z':c.transfer_protocol=protocol
        if args.action in ('receive','receive-in','receive-mail','receive-mail-many','receive-mail-many-resume','receive-many','receive-resume','receive-many-resume','receive-mail-resume'):
            if args.action in ('receive-in','receive-many','receive-resume','receive-many-resume'):
                category,separator,description=args.text.partition('|');c.upload_category='' if category=='-' else category;args.text=description if separator or args.action not in ('receive-resume','receive-many-resume') else None
            if protocol!='z' and args.action=='receive-in':
                ident,size,_=receive(c,engine,'127.0.0.1',args.port,args.user,args.spool_root,description=args.text)
            else:
                ident,size,_=runtime_receive(c,engine,'127.0.0.1',args.port,args.user,args.spool_root,description=args.text,batch=args.action in ('receive-mail-many','receive-mail-many-resume','receive-many','receive-many-resume'),resume=args.action in ('receive-mail-many-resume','receive-resume','receive-many-resume','receive-mail-resume'))
        elif args.action in ('batch','batch-mail'):
            ids=[int(x) for x in args.text.split(',')]
            if not 1<=len(ids)<=MAX_BATCH_FILES or len(ids)!=len(set(ids)) or any(not 1<=i<=0x7ffffffe for i in ids):raise TransferFailure('Invalid batch')
            with tempfile.TemporaryDirectory(prefix='liner-zbatch-',dir=args.spool_root) as directory:
                paths=[];snapshots={};total=0
                for ident in ids:
                    c.protocol.send('FILE '+str(ident));name=None
                    for _ in range(16):
                        line=c.protocol.line()
                        if line.startswith(b'ERR '):raise TransferFailure('File unavailable')
                        if line.startswith(b'NAME '):name=line[5:].decode('utf-8')
                        if line==b'END FILE':break
                    if not name:raise TransferFailure('Missing filename')
                    name=safe_name(name);length,digest=download_begin(c,ident);total+=length
                    if not 0<=length<=MAX_BYTES or total>MAX_BATCH_BYTES:raise TransferFailure('Batch exceeds 16 GiB')
                    path=batch_path(directory,name,ident)
                    hash_=hashlib.sha256();offset=0
                    with path.open('xb') as out:
                        while offset<length:
                            chunk=download_read(c,ident,offset)
                            if not chunk or offset+len(chunk)>length:raise TransferFailure('Invalid chunk')
                            out.write(chunk);hash_.update(chunk);offset+=len(chunk)
                        out.flush();os.fsync(out.fileno())
                    if hash_.hexdigest()!=digest:raise TransferFailure('SHA-256 mismatch')
                    snapshots[ident]=(length,digest);paths.append(path)
                def authorize():
                    for ident,metadata in snapshots.items():
                        if c.info(ident)!=metadata:raise TransferFailure('Batch file unavailable')
                completed_ids=set()
                def completed(path):
                    done_id=ids[paths.index(path)]
                    if done_id not in completed_ids:
                        download_done(c,done_id);completed_ids.add(done_id)
                engine.send_many(paths,0,1,authorize,completed=completed)
                if len(completed_ids)!=len(ids):raise TransferFailure('Incomplete batch receipts')
                ident=len(ids);size=total
        else:
            name=args.text
            if not name:
                c.protocol.send('FILE '+str(args.ident));name=None
                for _ in range(16):
                    line=c.protocol.line()
                    if line.startswith(b'ERR '):raise TransferFailure('File unavailable')
                    if line.startswith(b'NAME '):name=line[5:].decode('utf-8')
                    if line==b'END FILE':break
                if not name:raise TransferFailure('Missing filename')
            size,_=send(c,engine,args.ident,args.spool_root,name=name);ident=args.ident
        release_downloads()
        c.protocol.send(f'DONE OK {ident} {size}');return 0
    except PublicationFailure as error:
        release_downloads()
        c.protocol.send('DONE RECOVER '+ascii(str(error.directory)));return 2
    except (OSError,ValueError) as error:
        release_downloads()
        code=getattr(error,"code","TRANSFER")
        if code not in ("NO_PENDING","MISMATCH","RECEIVER","TIMEOUT"):code="TRANSFER"
        try:c.protocol.send("DONE FAIL "+code)
        except (OSError,ValueError):pass
        return 1
    finally:c.close()
if __name__=='__main__':raise SystemExit(main())
