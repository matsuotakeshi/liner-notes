import pathlib
import sys
import tempfile
from test_board import Server,post
from test_runtime import Client
from test_ui import Menu
from test_telnet import Client as TelnetClient

MORE=b'[Enter/Q/OFF]> '
JA='[続:Enter/止:Q/OFF]> '.encode()


def chunk(c):
    while True:
        for end in [MORE,JA,b'] > ']:
            if end in c.buf:
                n=c.buf.index(end)+len(end); out,c.buf=c.buf[:n],c.buf[n:]; return out,end
        if isinstance(c,TelnetClient): c.receive()
        else:
            data=c.sock.recv(8192); assert data; c.buf+=data


def command(c,text): c.send(text.encode()+b'\r\n'); return chunk(c)


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='pager-') as d:
        board=pathlib.Path(d)/'board'
        s=Server(binary,board); a=Client(s.port)
        body='\n'.join(f'row-{i:02d} 本文' for i in range(40))
        long='日本語'*90
        try: post(a,'paged',body); post(a,'long',long)
        finally: a.close(); s.close()
        snapshot=board.read_bytes()
        s=Server(binary,board,menu=True,language='en'); a,b=Menu(s.port),Menu(s.port)
        try:
            a.until(); b.until(); assert 'row-39' in a.cmd('READ 1')
            assert 'OFF' in a.cmd('PAGER'); a.cmd('PAGER ON'); a.cmd('PAGER SIZE 80 8')
            first,end=command(a,'READ 1'); assert end==MORE and b'row-39' not in first
            assert 'row-39' in b.cmd('READ 1'); assert 'PONG' in b.cmd('PING')
            combined=first.replace(MORE,b''); pages=1
            while end==MORE:
                out,end=command(a,''); combined+=out.replace(MORE,b''); pages+=1
                assert pages<30
            for i in range(40): assert combined.count(f'row-{i:02d}'.encode())==1
            assert pages>2
            command(a,'READ 1'); out,end=command(a,'Q'); assert end==b'] > ' and b'row-39' not in out
            command(a,'READ 1'); out,end=command(a,'OFF'); assert end==b'] > ' and b'row-39' in out
            assert 'OFF' in a.cmd('PAGER') and 'row-39' in a.cmd('READ 1')
            a.cmd('PAGER ON'); a.cmd('LANG ja'); out,end=command(a,'READ 1'); assert end==JA
            command(a,'/CANCEL'); a.cmd('LANG en'); a.cmd('PAGER SIZE 20 5')
            out,end=command(a,'READ 2'); combined=out.replace(MORE,b'')
            while end==MORE:
                out,end=command(a,''); combined+=out.replace(MORE,b'')
            text=combined.decode(); assert long in text.replace('\r\n','')
            assert 'width 20-240' in a.cmd('PAGER SIZE 0 0')
            a.cmd('RAW','RAW\r\n'); a.send(b'READ 1\n')
            raw=a.until('END\r\n'); assert 'row-39' in raw and 'Enter/Q/OFF' not in raw
            a.close(); a=Menu(s.port); a.until(); assert 'OFF' in a.cmd('PAGER')
        finally: a.close(); b.close(); s.close()
        print('PASS: default OFF, per-session toggle, Enter/stop/flush, Japanese/English, raw bypass and reconnect isolation')
        print('PASS: long UTF-8 lines wrap without splitting characters, all article text delivered once, other connections remain usable')
        s=Server(binary,board,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('PAGER ON')
            a.send(bytes([255,250,31,0,80,0,8,255,240]))
            out,end=command(a,'READ 1'); assert end==MORE and out.count(b'\r\n')<=6
            # A new NAWS height affects the next page of an already open article.
            a.send(bytes([255,250,31,0,80,0,60,255,240]))
            out,end=command(a,''); assert end==b'] > ' and b'row-39' in out
            a.send(bytes([255,250,31,0,0,0,0,255,240]))
            out,end=command(a,'READ 1'); assert end==MORE and b'row-39' not in out
            command(a,'Q')
        finally: a.close(); s.close()
        assert board.read_bytes()==snapshot
        print('PASS: Telnet NAWS dimensions, resize during paging, zero-size fallback and unchanged article storage')


if __name__=='__main__': run(sys.argv[1])
