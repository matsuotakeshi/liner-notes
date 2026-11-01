"""RTBBS M-LINK, YMODEM-g receive, and text adapters over transport FDs.
M-LINK framing follows TRANSSYS.PAS Mdownload/Mupload: M/header CR/M/data/E.
The engines have no account or vault storage access; callbacks recheck access.
"""
import binascii,datetime,os,pathlib,select,time
from zmodem_connector import TransferFailure,safe_name,staged_file,staged_files,MAX_BYTES,MAX_BATCH_BYTES,MAX_BATCH_FILES,raw_terminal,ZmodemProcess

class Wire:
    def __init__(self,readfd,writefd,timeout,authorize):
        self.readfd=readfd;self.writefd=writefd;self.timeout=timeout;self.deadline=time.monotonic()+timeout;self.check=0;self.authorize=authorize
    def ready(self,writing=False):
        while True:
            now=time.monotonic()
            if now>=self.deadline:raise TransferFailure('Transfer timed out','TIMEOUT')
            if now>=self.check:self.authorize();self.check=now+1
            r,w,_=select.select([] if writing else [self.readfd],[self.writefd] if writing else [],[],min(.25,self.deadline-now))
            if r or w:return
    def read(self,n):
        data=bytearray()
        while len(data)<n:
            self.ready();chunk=os.read(self.readfd,min(n-len(data),65536))
            if not chunk:raise TransferFailure('Peer disconnected')
            self.deadline=time.monotonic()+self.timeout
            data.extend(chunk)
        return bytes(data)
    def write(self,data):
        data=memoryview(data)
        while data:
            self.ready(True);n=os.write(self.writefd,data)
            if not n:raise TransferFailure('Peer disconnected')
            self.deadline=time.monotonic()+self.timeout
            data=data[n:]
    def until(self,terminator,limit):
        result=bytearray()
        while True:
            c=self.read(1)
            if c==terminator:return bytes(result)
            if c in (b'\x18',b'\x1b') or len(result)>=limit:raise TransferFailure('Cancelled or oversized header')
            result+=c
    def expect(self,c):
        if self.read(1)!=c:raise TransferFailure('Unexpected protocol response')

class LegacyEngine:
    def __init__(self,protocol,timeout=120,filename=None,expected_size=None,receiver=None,sender=None):
        if not 1<=timeout<=3600:raise TransferFailure('Timeout must be 1..3600 seconds')
        self.protocol=protocol;self.timeout=timeout;self.filename=filename;self.expected_size=expected_size;self.receiver=receiver;self.sender=sender
    def receive(self,directory,readfd,writefd,authorize):return self._receive(directory,readfd,writefd,authorize,False)[0]
    def receive_many(self,directory,readfd,writefd,authorize):
        if self.protocol=='t':raise TransferFailure('Text batch unsupported')
        return self._receive(directory,readfd,writefd,authorize,True)
    def _receive(self,directory,readfd,writefd,authorize,batch):
        directory=pathlib.Path(directory)
        if list(directory.iterdir()):raise TransferFailure('Receive directory must be empty')
        wire=Wire(readfd,writefd,self.timeout,authorize)
        with raw_terminal(readfd):
            if self.protocol=='g':self._yg_receive(directory,wire,batch)
            elif self.protocol=='m':self._m_receive(directory,wire,batch)
            elif self.protocol=='t':
                path=directory/safe_name(self.filename or 'received.txt');size=0
                with path.open('xb') as f:
                    while True:
                        byte=wire.read(1)
                        if byte==b'\x1a':break
                        if byte in (b'\x18',b'\x1b') or size>=MAX_BYTES:raise TransferFailure('Text cancelled or oversized')
                        f.write(byte);size+=1
                    if self.expected_size and size!=self.expected_size:raise TransferFailure('Text size mismatch')
                    f.flush();os.fsync(f.fileno())
            else:raise TransferFailure('Unsupported protocol')
        authorize();return staged_files(directory,True) if batch else [staged_file(directory,True)]
    def _m_receive(self,directory,wire,batch):
        total=0;count=0
        while True:
            wire.write(b'M');header=wire.until(b'\r',200)
            if not header:
                if not count:raise TransferFailure('Empty M-LINK transfer')
                wire.write(b'E');return
            fields=header.split(b' ')
            if len(fields)!=3 or not fields[1].isdigit():raise TransferFailure('Invalid M-LINK header')
            name=safe_name(fields[0].decode('utf-8'));size=int(fields[1]);total+=size;count+=1
            if size>MAX_BYTES or total>MAX_BATCH_BYTES or count>MAX_BATCH_FILES or (not batch and count>1):raise TransferFailure('M-LINK batch limit')
            with (directory/name).open('xb') as f:
                wire.write(b'M');left=size
                while left:
                    block=wire.read(min(left,65536));f.write(block);left-=len(block)
                f.flush();os.fsync(f.fileno())
            wire.write(b'E')
            if not batch:
                # A legacy sender terminates the batch with CR after the final E.
                wire.expect(b'\r');wire.write(b'E');return
    @staticmethod
    def _packet(wire):
        start=wire.read(1)
        if start==b'\x04':return None
        if start not in (b'\x01',b'\x02'):raise TransferFailure('YMODEM-g cancelled or invalid packet')
        size=128 if start==b'\x01' else 1024
        seq,inv=wire.read(2);data=wire.read(size);crc=int.from_bytes(wire.read(2),'big')
        if seq^inv!=255 or binascii.crc_hqx(data,0)!=crc:raise TransferFailure('YMODEM-g CRC error')
        return seq,data
    def _yg_receive(self,directory,wire,batch):
        count=total=0
        while True:
            wire.write(b'G');packet=self._packet(wire)
            if packet is None or packet[0]!=0:raise TransferFailure('Missing YMODEM-g header')
            name,sep,meta=packet[1].partition(b'\0')
            if not name:return
            fields=meta.split(b'\0',1)[0].split()
            if not sep or not fields or not fields[0].isdigit():raise TransferFailure('Missing file size')
            name=safe_name(name.decode('utf-8'));length=int(fields[0]);total+=length;count+=1
            if length>MAX_BYTES or total>MAX_BATCH_BYTES or count>MAX_BATCH_FILES or (not batch and count>1):raise TransferFailure('YMODEM-g batch limit')
            with (directory/name).open('xb') as f:
                wire.write(b'G');remaining=length;sequence=1
                while True:
                    packet=self._packet(wire)
                    if packet is None:
                        if remaining:raise TransferFailure('Premature YMODEM-g EOF')
                        f.flush();os.fsync(f.fileno());wire.write(b'\x06');break
                    seq,data=packet
                    if seq!=sequence or remaining<=0:raise TransferFailure('YMODEM-g sequence mismatch')
                    f.write(data[:remaining]);remaining-=min(remaining,len(data));sequence=(sequence+1)&255
    def send(self,path,readfd,writefd,authorize):return self.send_many([path],readfd,writefd,authorize)
    def send_many(self,paths,readfd,writefd,authorize,completed=None):
        paths=[pathlib.Path(p) for p in paths]
        if not 1<=len(paths)<=MAX_BATCH_FILES or any(p.stat().st_size>MAX_BYTES for p in paths) or sum(p.stat().st_size for p in paths)>MAX_BATCH_BYTES:raise TransferFailure('Batch limit')
        for p in paths:safe_name(p.name)
        wire=Wire(readfd,writefd,self.timeout,authorize)
        with raw_terminal(readfd):
            if self.protocol=='g':
                def packet(seq,data,size):
                    data=data.ljust(size,b'\x1a');wire.write((b'\x01' if size==128 else b'\x02')+bytes((seq,255-seq))+data+binascii.crc_hqx(data,0).to_bytes(2,'big'))
                for path in paths:
                    wire.expect(b'G');header=path.name.encode()+b'\0'+str(path.stat().st_size).encode()+b'\0'
                    if len(header)>128:raise TransferFailure('YMODEM header too long')
                    packet(0,header.ljust(128,b'\0'),128);wire.expect(b'G');sequence=1
                    with path.open('rb') as f:
                        while block:=f.read(1024):packet(sequence,block,1024);sequence=(sequence+1)&255
                    wire.write(b'\x04');wire.expect(b'\x06')
                    if completed:completed(path)
                wire.expect(b'G');packet(0,b'\0'*128,128)
            elif self.protocol=='t':
                if len(paths)!=1:raise TransferFailure('Text batch unsupported')
                # Validate before exposing any content or falsely delimiting a file.
                with paths[0].open('rb') as f:
                    while block:=f.read(65536):
                        if any(x<32 and x not in (9,10,13) or x==127 for x in block):raise TransferFailure('Not a text file')
                with paths[0].open('rb') as f:
                    while block:=f.read(65536):wire.write(block)
                wire.write(b'\x1a')
            elif self.protocol=='m':
                for path in paths:
                    if ' ' in path.name:raise TransferFailure('M-LINK filename cannot contain spaces')
                    wire.expect(b'M');wire.write(f'{path.name} {path.stat().st_size} {datetime.datetime.now(datetime.timezone.utc):%Y%m%d%H%M}\r'.encode());wire.expect(b'M')
                    with path.open('rb') as f:
                        while block:=f.read(65536):wire.write(block)
                    wire.expect(b'E')
                    if completed:completed(path)
                wire.write(b'\r')
            else:raise TransferFailure('Unsupported protocol')
        authorize()
