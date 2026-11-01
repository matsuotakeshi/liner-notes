"""Exclusive binary framing over raw TCP/Telnet, resume and normal-mode restoration."""
import hashlib
import pathlib
import subprocess
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth, PASSWORD
from test_network_transfer import uploader, fails
from transfer_connectors import BinaryConnector, LineConnector, FrameProtocol


def run(binary,ctl):
    binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
    for telnet in (False,True):
        with tempfile.TemporaryDirectory(prefix='binary-transfer-') as d:
            p=pathlib.Path(d);vault=p/'vault';users=p/'users';data=p/'bbs'
            subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
            s=Server(binary,data,members=users)
            c=Client(s.port)
            try:auth(c,'alice',True)
            finally:c.close();s.close()
            s=Server(binary,data,members=users,vault=vault,telnet=telnet,menu=True,language='en')
            payload=bytes(range(256))*13+b'\r\n\x1b[2J\x7f\xff\x00';source=p/'source';source.write_bytes(payload)
            try:
                def interrupted(done,total):
                    if done>=768:raise OSError('interrupted')
                args=('127.0.0.1',s.port,'alice',PASSWORD,source,p/'state.json')
                fails(lambda:uploader.upload(*args,connector_factory=BinaryConnector,progress=interrupted),'interrupted')
                ident,size,digest=uploader.upload(*args,connector_factory=BinaryConnector,resume=True)
                assert size==len(payload) and digest==hashlib.sha256(payload).hexdigest()
                c=BinaryConnector('127.0.0.1',s.port,'alice',PASSWORD)
                try:
                    assert c.info(ident)==(size,digest)
                    received=b''
                    while len(received)<size:received+=c.read(ident,len(received))
                    assert received==payload and c.read(ident,size)==b''
                    # One-byte writes fragment both the header and control payload.
                    request=b'LFILEMETA '+str(ident).encode()
                    for byte in b'~'+len(request).to_bytes(4,'big')+request:c.protocol.wire.send_bytes(bytes([byte]))
                    response=c.protocol.frame();assert response.startswith(b'TFILE ') and response.endswith(b'END FILE\r\n')
                    c.leave();assert c.protocol.request('PING',lambda x:x==b'PONG')==b'PONG'
                finally:c.close()
                # MENU must be restored after leaving the exclusive mode.
                c=LineConnector('127.0.0.1',s.port,'alice',PASSWORD)
                try:
                    c.protocol.send('MENU');c.protocol.send('TRANSFER BINARY')
                    c.protocol.await_response(lambda x:x.endswith(b'BINARY READY binary-v1'))
                    frames=FrameProtocol(c.protocol);frames.send_frame(b'Q');assert frames.frame()==b'E'
                    c.protocol.binary_stream=False
                    c.protocol.await_response(lambda x:x==b'BINARY CLOSED')
                    while b'> ' not in c.protocol.buf:c.protocol.receive()
                    assert b'Liner notes' in c.protocol.buf or b'Help' in c.protocol.buf,bytes(c.protocol.buf)
                finally:c.close()
                if telnet:
                    c=BinaryConnector('127.0.0.1',s.port,'alice',PASSWORD)
                    try:
                        c.protocol.wire.sock.sendall(bytes([255,252,0]))
                        fails(lambda:c.protocol.frame())
                    finally:c.close()
                for malformed in (b'x',b'~\0\0\0\0',b'~\0\0\4\1',b'~\0\0\0\1X',b'~\0\0\0\5LPING'):
                    c=BinaryConnector('127.0.0.1',s.port,'alice',PASSWORD)
                    try:
                        c.protocol.wire.send_bytes(malformed)
                        fails(lambda:c.protocol.frame())
                    finally:c.close()
            finally:s.close()
    print('PASS: binary-v1 TCP/Telnet all-byte roundtrip, resume, fragmented frames, menu restore and malformed-frame isolation')

if __name__=='__main__':run(*sys.argv[1:])
