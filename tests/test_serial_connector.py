"""Real PTYs: all binary byte values, fragmented Telnet, modem answer, BBS login."""
from pathlib import Path
from unittest.mock import patch
import concurrent.futures,os,pty,select,socket,subprocess,sys,tempfile,termios,time
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
import serial_connector as sc
from test_board import Server

def receive(fd,expected,timeout=10):
    result=bytearray();end=time.monotonic()+timeout
    while expected not in result:
        left=end-time.monotonic()
        assert left>0,bytes(result)
        assert select.select([fd],[],[],left)[0],bytes(result)
        data=os.read(fd,8192);assert data;result.extend(data)
    return bytes(result)

def run(binary):
    assert sc.platform_profile('freebsd','freebsd15')=='freebsd'
    assert sc.platform_profile('linux','linux')=='linux'
    for requested,host in (('linux','freebsd15'),('freebsd','linux'),('auto','win32')):
        try:sc.platform_profile(requested,host)
        except ValueError:pass
        else:raise AssertionError('Mismatched or unsupported platform accepted')
    master,slave=pty.openpty();path=os.ttyname(slave);saved=termios.tcgetattr(slave)
    try:
        # Failed ownership acquisition must not clear another owner's TIOCEXCL.
        with patch.object(sc.fcntl,'flock',side_effect=BlockingIOError('busy')), patch.object(sc.fcntl,'ioctl') as ioctl:
            try:
                with sc.device(path,115200,'none'):raise AssertionError('Busy device acquired')
            except BlockingIOError:pass
            ioctl.assert_not_called()
        with sc.device(path,115200,'none') as fd, concurrent.futures.ThreadPoolExecutor() as pool:
            answer=pool.submit(sc.modem_answer,fd,'ATE0',2)
            receive(master,b'ATE0\r');os.write(master,b'OK\r\nRING\r\n');receive(master,b'ATA\r')
            os.write(master,b'CONNECT 115200\r');answer.result(timeout=3)
            listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen()
            client=socket.create_connection(listener.getsockname());peer,_=listener.accept();listener.close();peer.settimeout(5)
            future=pool.submit(sc.relay,fd,client,timeout=5)
            codec=sc.Telnet();body=bytes(range(256))*32
            # Forced single-byte control fragments must not leak onto the PTY.
            for b in b'\xff\xfb\x01\xff\xfb\x00':peer.sendall(bytes([b]))
            assert peer.recv(3)==b'\xff\xfd\x01'
            assert peer.recv(3)==b'\xff\xfd\x00'
            peer.sendall(body.replace(b'\xff',b'\xff\xff'));assert receive(master,body)==body
            os.write(master,body);decoded=bytearray()
            while len(decoded)<len(body):
                data,reply=codec.feed(peer.recv(8192));assert not reply;decoded.extend(data)
            assert decoded==body
            peer.sendall(b'final message');peer.shutdown(socket.SHUT_WR)
            assert receive(master,b'final message')==b'final message'
            assert future.result(timeout=3)=='BBS closed'
            client.close();peer.close()
        assert termios.tcgetattr(slave)==saved
    finally:os.close(master);os.close(slave)
    with tempfile.TemporaryDirectory(prefix='serial73-') as tmp:
        p=Path(tmp);server=Server(str(Path(binary).resolve()),p/'notes',members=p/'members',telnet=True)
        master,slave=pty.openpty();path=os.ttyname(slave)
        process=subprocess.Popen([sys.executable,str(ROOT/'scripts/serial_connector.py'),'--platform',sc.platform_profile(),'--device',path,'--port',str(server.port),'--flow','none','--ignore-carrier','--once'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        try:
            receive(master,b'RTBBS-PROBE');os.write(master,b'REGISTER serialuser\r\n');receive(master,b'PASSWORD')
            os.write(master,b'serial-test-password\r\n');response=receive(master,b'LOGGED IN')
            assert b'serial-test-password' not in response
            os.write(master,b'PING\r\n');receive(master,b'PONG')
            os.write(master,b'QUIT\r\n');process.wait(timeout=8);assert process.returncode==0,process.stderr.read()
        finally:
            if process.poll() is None:process.terminate();process.wait(timeout=5)
            os.close(master);os.close(slave);server.close()
    print('PASS serial PTY: binary round trip, fragmented Telnet/hidden password, modem RING/ATA/CONNECT, EOF drain, termios restore, real BBS registration and logout')

if __name__=='__main__':run(sys.argv[1])

