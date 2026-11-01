"""Telnet integration over real sockets, with independent client-side decoding."""
import pathlib
import socket
import sys
import tempfile
import time
import threading
from test_board import Server

IAC=255


class Client:
    def __init__(self,port,echo=True,host='127.0.0.1'):
        self.sock=socket.create_connection((host,port),timeout=10)
        self.buf=b''; self.wire=b''; self.state=0; self.verb=0; self.echo=echo
        self.options=[]

    def send(self,data): self.sock.sendall(data)

    def receive(self):
        data=self.sock.recv(8192)
        assert data,'unexpected disconnect'
        self.wire+=data
        for b in data:
            if self.state==0:
                if b==255: self.state=1
                else: self.buf+=bytes([b])
            elif self.state==1:
                if b==255: self.buf+=b'\xff'; self.state=0
                elif b in [251,252,253,254]: self.verb=b; self.state=2
                elif b==250: self.state=3
                else: self.state=0
            elif self.state==2:
                self.options.append((self.verb,b))
                if self.verb==251:
                    self.send(bytes([255,253 if b in [0,3] or (b==1 and self.echo) else 254,b]))
                elif self.verb==253:
                    self.send(bytes([255,251 if b in [0,3,31] else 252,b]))
                self.state=0
            elif self.state==3:
                if b==255: self.state=4
            elif self.state==4: self.state=0 if b==240 else 3

    def until(self,marker=b'] > '):
        while marker not in self.buf: self.receive()
        end=self.buf.index(marker)+len(marker)
        result,self.buf=self.buf[:end],self.buf[end:]
        return result

    def cmd(self,cmd,marker=b'] > '):
        if isinstance(cmd,str): cmd=cmd.encode()
        self.send(cmd+b'\r\n'); return self.until(marker)

    def close(self): self.sock.close()


def disconnected(c):
    try:
        while c.sock.recv(8192): pass
    except ConnectionResetError: pass


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='telnet-') as d:
        p=pathlib.Path(d)
        s=Server(binary,p/'board',members=p/'members',telnet=True,menu=True,language='en')
        a,b=Client(s.port),Client(s.port)
        try:
            assert b'Liner notes' in a.until(); b.until()
            assert {(251,1),(251,3),(251,0),(253,3),(253,0),(253,31)}<=set(a.options)
            # Command and password in ONE write: no password byte may be echoed.
            a.send(b'REGISTER alice\r\npipeline-secret-8721\r\n')
            result=a.until(); assert b'Logged in as alice' in result
            assert b'pipeline-secret' not in a.wire
            assert b'Guest' in b.cmd('ACCOUNT')
            for byte in b'PI\xff\xfb\x63NG\r\n': a.send(bytes([byte]))
            assert b'PONG' in a.until(); assert (254,99) in a.options
            # Escaped IAC is data, not a command; transmission doubles it again.
            result=a.cmd(b'ECHO \xff\xff'); assert b'\xff' in result
            assert b'\xff\xff' in a.wire
            for command in [b'PINX\x08G',b'PINX\x7fG',b'PINX\xff\xf7G',b'wrong\xff\xf8PING',b'wrong\x15PING',b'PI\x1b[ANG']:
                assert b'PONG' in a.cmd(command)
            assert '本'.encode() in a.cmd('ECHO 日\x7f本')
            a.send(b'PING\r\x00PING\n'); assert b'PONG' in a.until(); assert b'PONG' in a.until()
            # Unknown subnegotiations must not become posted text/commands.
            a.send(b'\xff\xfa\x63QUIT\xff\xffPING\xff\xf0PING\n')
            assert b'PONG' in a.until()
            a.cmd('POST',b'> '); a.cmd('Telnet note',b'Body > ')
            a.cmd('Telnet body',b'Body > '); assert b'Saved article #1' in a.cmd('.')
            assert b'Telnet body' in a.cmd('1')
            assert '日本語'.encode() in a.cmd('LANG ja'); assert '記事 #1'.encode() in a.cmd('1')
            a.cmd('LANG en')
            # Slow partial negotiation cannot block another client.
            a.send(b'\xff'); start=time.monotonic(); assert b'PONG' in b.cmd('PING'); assert time.monotonic()-start<1
            a.send(b'\xf1PING\n'); assert b'PONG' in a.until()
            a.cmd('LOGOUT'); time.sleep(1.1)
            a.cmd('LOGIN alice',b'> ')
            a.send(b'pipeline-secret-872X\x7f1\r\n')
            response=a.until(); assert b'Logged in as alice' in response
            assert b'pipeline-secret' not in a.wire
            print('PASS: fragmented Telnet/IAC, negotiation, echo, UTF-8 erase, EC/EL, CRLF/CRNUL/LF and subnegotiation filtering')
            print('PASS: pipelined and edited passwords never echoed; real register/login/post/read/language operations')
            print('PASS: incomplete control sequences do not block other sessions')
            # Disconnect with a partial IAC, then reconnect with fresh state.
            a.send(b'\xff\xfa\x63partial'); a.close(); a=Client(s.port); a.until()
            assert b'PONG' in a.cmd('PING')
            a.send(b'\xff\xfa\x63'+b'x'*1025); disconnected(a)
            a.close(); a=Client(s.port); a.until()
            a.send(b'x'*4097); disconnected(a)
            assert b'PONG' in b.cmd('PING')
            a.close(); a=Client(s.port,echo=False); a.until()
            a.send(b'LOGIN alice\n'); disconnected(a)
            print('PASS: reconnect isolation, bounded subnegotiation/line rejection and refused-echo login closure')
            b.cmd('Q',b'(Y/[N]) > '); b.send(b'Y\r\n'); assert b'Thank you' in b.until(b'Thank you. Goodbye.\r\n'); disconnected(b)
        finally: a.close(); b.close(); s.close()


if __name__=='__main__': run(sys.argv[1])
