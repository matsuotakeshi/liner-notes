"""Client connector contract and line-v1 implementation (no filesystem access).

Future protocols implement TransferConnector; workflows remain protocol-neutral.
binary-v1 adds an exclusive byte-preserving mode. ZMODEM is not implemented.
"""
import base64
import re
import socket
import time
from abc import ABC, abstractmethod

class RemoteError(ValueError):
    """An explicit error response from the server, distinct from a lost reply."""


class Protocol:
    def __init__(self, host, port):
        self.sock=socket.create_connection((host,port),timeout=15)
        self.telnet=False;self.binary_stream=False
        self.buf=bytearray(); self.state=0; self.verb=0; self.subsize=0

    def close(self):
        self.sock.close()

    def send(self, text):
        self.sock.sendall(text.encode('utf-8').replace(b'\xff',b'\xff\xff')+b'\r\n')

    def receive(self):
        data=self.sock.recv(4096)
        if not data: raise ValueError('Connection closed')
        if self.binary_stream and not self.telnet:
            self.buf.extend(data)
            if len(self.buf)>32768:raise ValueError('Oversized response')
            return
        for b in data:
            if self.state==0:
                if b==255:self.state=1;self.telnet=True
                else:self.buf.append(b)
            elif self.state==1:
                if b==255:self.buf.append(b);self.state=0
                elif b in (251,252,253,254):self.verb=b;self.state=2
                elif b==250:self.state=3;self.subsize=0
                else:self.state=0
            elif self.state==2:
                if self.verb==251:self.sock.sendall(bytes([255,253 if b in (0,1,3) else 254,b]))
                elif self.verb==253:self.sock.sendall(bytes([255,251 if b in (0,3) else 252,b]))
                self.state=0
            elif self.state==3:
                self.subsize+=1
                if self.subsize>1024:raise ValueError('Oversized Telnet control')
                if b==255:self.state=4
            elif self.state==4:self.state=0 if b==240 else 3
        if len(self.buf)>32768:raise ValueError('Oversized response')
    def exact(self, size):
        while len(self.buf)<size:self.receive()
        data=bytes(self.buf[:size]);del self.buf[:size];return data

    def send_bytes(self, data):self.sock.sendall(data.replace(b'\xff',b'\xff\xff') if self.telnet else data)

    def line(self):
        while b'\n' not in self.buf:self.receive()
        line,_,rest=self.buf.partition(b'\n');self.buf=bytearray(rest)
        return bytes(line).rstrip(b'\r')

    def await_response(self, predicate):
        for _ in range(128):
            line=self.line()
            if line.startswith(b'ERR '):raise RemoteError(line.decode('utf-8',errors='replace'))
            if predicate(line):return line
        raise ValueError('Expected response not found')

    def request(self, command, predicate):
        for attempt in range(20):
            self.send(command)
            try:return self.await_response(predicate)
            except ValueError as error:
                if str(error)!='ERR vault busy' or attempt==19:raise
                time.sleep(.1)

    def info(self, ident):
        line=self.request(f'FILEMETA {ident}',lambda x:x.startswith(b'FILE ') and len(x.split())==4)
        _,number,size,digest=line.split()
        if int(number)!=ident or not re.fullmatch(b'[0-9a-f]{64}',digest):raise ValueError('Invalid metadata')
        size=int(size)
        if not 0<=size<=1024*1024*1024:raise ValueError('File exceeds client limit')
        self.await_response(lambda x:x==b'END FILE')
        return size,digest.decode()


class TransferConnector(ABC):
    """Authenticated connection. Metadata/chunks contain no server paths.

    A future connector may negotiate binary streams internally. Workflows only
    depend on these source/sink operations, not Telnet or base64 framing.
    """
    name = 'abstract'
    chunk_size = 672
    @abstractmethod
    def close(self): ...
    @abstractmethod
    def info(self, ident): ...
    @abstractmethod
    def read(self, ident, offset): ...
    @abstractmethod
    def begin(self, size, digest, name, description): ...
    @abstractmethod
    def status(self, ident): ...
    @abstractmethod
    def write(self, ident, offset, data): ...
    @abstractmethod
    def finish(self, ident): ...
    @abstractmethod
    def cancel(self, ident): ...


class LineConnector(TransferConnector):
    name = 'line-v1'
    def __init__(self, host, port, user, password):
        if not re.fullmatch(r'[a-z0-9_-]{1,32}',user):raise ValueError('Invalid login name')
        if not 8<=len(password.encode('utf-8'))<=128 or any(c in password for c in '\r\n\0'):
            raise ValueError('Invalid password length or characters')
        self.protocol=Protocol(host,port)
        try:
            self.protocol.request('RAW',lambda x:x==b'RAW')
            self.protocol.request('LOGIN '+user,lambda x:x==b'PASSWORD')
            self.protocol.send(password);self.protocol.await_response(lambda x:x.startswith(b'LOGGED IN '))
        except BaseException:self.close();raise
    def close(self):
        for ident in list(getattr(self,"_downloads",{})):download_abort(self,ident)
        self.protocol.close()
    def info(self, ident):return self.protocol.info(ident)
    def read(self, ident, offset):
        line=self.protocol.request(f'FILEGET {ident} {offset}',lambda x:x.startswith(b'FILEDATA '))
        parts=line.split()
        if len(parts)!=5 or int(parts[1])!=ident or int(parts[2])!=offset:raise ValueError('Invalid chunk header')
        count=int(parts[3]);data=b'' if parts[4]==b'-' else base64.b64decode(parts[4],validate=True)
        if count!=len(data) or not 0<=count<=768:raise ValueError('Invalid chunk size')
        return data
    def begin_download(self, ident):
        if self.name == 'mail-line-v1':return self.info(ident)
        active=getattr(self,'_downloads',{})
        if ident in active:raise ValueError('Download already active')
        row=self.protocol.request(f'FILEOPEN {ident}',lambda x:x.startswith(b'DOWNLOAD ')).split()
        if len(row)!=4 or not re.fullmatch(b'[0-9a-f]{32}',row[1]) or not re.fullmatch(b'[0-9a-f]{64}',row[3]) or not 0<=int(row[2])<=1024*1024*1024:raise ValueError('Invalid download ticket')
        active[ident]=(row[1].decode(),row[3].decode());self._downloads=active
        return int(row[2]),row[3].decode()
    def read_download(self, ident, offset):
        if self.name == 'mail-line-v1':return self.read(ident,offset)
        token,_=self._downloads[ident]
        row=self.protocol.request(f'FILEFETCH {token} {offset}',lambda x:x.startswith(b'DOWNLOADDATA ')).split()
        if len(row)!=4 or int(row[1])!=offset:raise ValueError('Invalid download chunk')
        data=b'' if row[3]==b'-' else base64.b64decode(row[3],validate=True)
        if len(data)!=int(row[2]) or len(data)>768:raise ValueError('Invalid download chunk size')
        return data
    def complete_download(self, ident):
        if self.name == 'mail-line-v1':return
        token,digest=self._downloads[ident]
        self.protocol.request(f'FILEDONE {token} {digest}',lambda x:x==f'DOWNLOADED {ident}'.encode())
        del self._downloads[ident]
    def abort_download(self, ident):
        item=getattr(self,'_downloads',{}).pop(ident,None)
        if item:self.protocol.request(f'FILEABORT {item[0]}',lambda x:x==b'DOWNLOAD ABORTED')
    def begin(self, size, digest, name, description):
        encoded=lambda text:base64.b64encode(text.encode('utf-8')).decode() or '-'
        category=getattr(self,'upload_category','')
        command=f'FILESTARTIN {size} {digest} {encoded(name)} {encoded(description)} {encoded(category)}' if category else f'FILESTART {size} {digest} {encoded(name)} {encoded(description)}'
        line=self.protocol.request(command,lambda x:x.startswith(b'UPLOAD '))
        parts=line.split()
        if len(parts)!=2 or not 1<=int(parts[1])<=0x7ffffffe:raise ValueError('Invalid upload ID')
        return int(parts[1])
    def check_upload(self):
        category=getattr(self,'upload_category','')
        token=base64.b64encode(category.encode()).decode() or '-'
        self.protocol.request('FILECHECK '+token,lambda x:x==b'FILEBOARD READY')
    def status(self, ident):
        line=self.protocol.request(f'FILESTATUS {ident}',lambda x:x.startswith(b'UPLOADSTATUS '));parts=line.split()
        if len(parts)!=6 or int(parts[1])!=ident or parts[2] not in (b'ACTIVE',b'COMMITTED',b'CANCELLED') or not re.fullmatch(b'[0-9a-f]{64}',parts[5]):raise ValueError('Invalid upload status')
        offset,size=int(parts[3]),int(parts[4])
        if not 0<=offset<=size<=1024*1024*1024:raise ValueError('Invalid upload size')
        return parts[2].decode(),offset,size,parts[5].decode()
    def write(self, ident, offset, data):
        if not 1<=len(data)<=self.chunk_size:raise ValueError('Invalid upload chunk')
        payload=base64.b64encode(data).decode()
        line=self.protocol.request(f'FILEPUT {ident} {offset} {payload}',lambda x:x.startswith(b'RECEIVED '));parts=line.split()
        if len(parts)!=3 or int(parts[1])!=ident or int(parts[2])!=offset+len(data):raise ValueError('Invalid write acknowledgement')
        return int(parts[2])
    def finish(self, ident):
        line=self.protocol.request(f'FILEFINISH {ident}',lambda x:x.startswith(b'UPLOADED '));parts=line.split()
        if len(parts)!=4 or int(parts[1])!=ident or not 0<=int(parts[2])<=1024*1024*1024 or not re.fullmatch(b'[0-9a-f]{64}',parts[3]):raise ValueError('Invalid commit receipt')
        return int(parts[2]),parts[3].decode()
    def cancel(self, ident):
        self.protocol.request(f'FILECANCEL {ident}',lambda x:x==f'UPLOAD CANCELLED {ident}'.encode())


class FrameProtocol:
    def __init__(self, wire):self.wire=wire;self.lines=[];wire.binary_stream=True
    def close(self):self.wire.close()
    def send_frame(self, payload):
        if not 1<=len(payload)<=1024:raise ValueError('Invalid outgoing frame length')
        self.wire.send_bytes(b'~'+len(payload).to_bytes(4,'big')+payload)
    def frame(self):
        header=self.wire.exact(5)
        if header[0]!=126:raise ValueError('Binary framing lost')
        size=int.from_bytes(header[1:],'big')
        if not 1<=size<=2048:raise ValueError('Invalid incoming frame length')
        return self.wire.exact(size)
    def send(self, text):self.send_frame(b'L'+text.encode('utf-8'))
    def line(self):
        if not self.lines:
            payload=self.frame()
            if payload[:1]!=b'T':raise ValueError('Expected text frame')
            self.lines=payload[1:].splitlines()
            if not self.lines:raise ValueError('Empty text frame')
        return self.lines.pop(0)
    await_response=Protocol.await_response
    request=Protocol.request
    info=Protocol.info
    def raw_request(self,payload):
        if self.lines:raise ValueError('Unconsumed response')
        for attempt in range(20):
            self.send_frame(payload);response=self.frame()
            if response.startswith(b'TERR '):
                error=response[1:].decode('utf-8',errors='replace').strip()
                if error=='ERR vault busy' and attempt<19:time.sleep(.1);continue
                raise RemoteError(error)
            return response


class BinaryConnector(LineConnector):
    name='binary-v1'
    chunk_size=768
    def __init__(self,host,port,user,password):
        super().__init__(host,port,user,password)
        try:
            self.protocol.request('TRANSFER BINARY',lambda x:x==b'BINARY READY binary-v1')
            self.protocol=FrameProtocol(self.protocol)
        except BaseException:self.close();raise
    def read(self,ident,offset):
        reply=self.protocol.raw_request(b'R'+ident.to_bytes(4,'big')+offset.to_bytes(4,'big'))
        if len(reply)<9 or reply[0]!=ord('D') or int.from_bytes(reply[1:5],'big')!=ident or int.from_bytes(reply[5:9],'big')!=offset or len(reply)>777:raise ValueError('Invalid binary data frame')
        return reply[9:]
    def write(self,ident,offset,data):
        if not 1<=len(data)<=self.chunk_size:raise ValueError('Invalid binary chunk')
        reply=self.protocol.raw_request(b'W'+ident.to_bytes(4,'big')+offset.to_bytes(4,'big')+data)
        if reply!=f'TRECEIVED {ident} {offset+len(data)}\r\n'.encode():raise ValueError('Invalid binary write acknowledgement')
        return offset+len(data)
    def leave(self):
        self.protocol.send_frame(b'Q')
        if self.protocol.frame()!=b'E':raise ValueError('Invalid binary exit')
        wire=self.protocol.wire;wire.binary_stream=False
        wire.await_response(lambda x:x==b'BINARY CLOSED')
        self.protocol=wire


class MailProtocol:
    def __init__(self, wire):self.wire=wire
    def __getattr__(self, key):return getattr(self.wire,key)
    def send(self, text):self.wire.send('M'+text if text.startswith('FILE') else text)
    await_response=Protocol.await_response
    request=Protocol.request
    info=Protocol.info

class MailConnector(LineConnector):
    name='mail-line-v1'
    def __init__(self,*args):
        super().__init__(*args);self.protocol=MailProtocol(self.protocol)

CONNECTORS = {MailConnector.name:MailConnector,LineConnector.name:LineConnector,BinaryConnector.name:BinaryConnector}

# Workflows also support third-party connectors which have no receipt capability.
# Such connectors retain their existing behavior and do not increment counts.
def download_begin(connection,ident):
    return getattr(connection,'begin_download',connection.info)(ident)
def download_read(connection,ident,offset):
    return getattr(connection,'read_download',connection.read)(ident,offset)
def download_done(connection,ident):
    method=getattr(connection,'complete_download',None)
    if method:method(ident)
def download_abort(connection,ident):
    method=getattr(connection,'abort_download',None)
    if method:
        try:method(ident)
        except (OSError,ValueError):pass
