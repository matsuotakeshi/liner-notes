"""Exercise the host's real Telnet client through a pseudo terminal (POSIX)."""
import os
import pathlib
import select
import shutil
import signal
import sys
import tempfile
import time
from test_board import Server


def run(binary,entry=None):
    if os.name!='posix':
        print('SKIP: native Telnet PTY check requires POSIX'); return
    import pty
    client=shutil.which('telnet')
    if not client:
        print('SKIP: native telnet client is not installed'); return
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='telnet-client-') as d:
        p=pathlib.Path(d)
        s=Server(binary,p/'board',members=p/'users',telnet=True,menu=True,language='en')
        pid,fd=pty.fork()
        if pid==0:
            os.environ['TERM']='xterm'
            if entry:
                os.execv('/bin/sh',['/bin/sh',entry,str(s.port)])
            os.execv(client,[client,'-8','127.0.0.1',str(s.port)])
        buf=b''
        def until(marker):
            nonlocal buf
            deadline=time.monotonic()+10
            while marker not in buf:
                left=deadline-time.monotonic()
                assert left>0,('client timeout',buf)
                ready,_,_=select.select([fd],[],[],left)
                assert ready,('client timeout',buf)
                chunk=os.read(fd,8192); assert chunk,buf; buf+=chunk
            end=buf.index(marker)+len(marker); result,buf=buf[:end],buf[end:]; return result
        try:
            assert b'Liner notes' in until(b'] > ')
            os.write(fd,b'REGISTER nativeclient\r'); until(b'Password (8-128 bytes)> ')
            os.write(fd,b'native-client-secret-2468\r')
            result=until(b'] > ')
            assert b'Logged in as nativeclient' in result
            assert b'native-client-secret' not in result, 'native client echoed password'
            os.write(fd,(b'\x1d' if entry else b'')+b'PING\r'); assert b'PONG' in until(b'] > ')
            os.write(fd,b'POST\r'); until(b'title (up to 80 bytes)> ')
            os.write(fd,b'Native Telnet\r'); until(b'Body > ')
            os.write(fd,b'Written through a real terminal\r'); until(b'Body > ')
            os.write(fd,b'.\r'); assert b'Saved article #1' in until(b'] > ')
            os.write(fd,b'1\r'); assert b'Written through a real terminal' in until(b'] > ')
            os.write(fd,b'Q\r'); until(b'(Y/[N]) > '); os.write(fd,b'Y\r'); until(b'Thank you. Goodbye.')
            print('PASS: native Telnet client via PTY: register, no local password echo, post/read and quit')
        finally:
            os.close(fd)
            try: os.kill(pid,signal.SIGTERM)
            except ProcessLookupError: pass
            os.waitpid(pid,0); s.close()


if __name__=='__main__': run(sys.argv[1])
