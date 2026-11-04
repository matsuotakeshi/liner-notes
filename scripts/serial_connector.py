#!/usr/bin/env python3
"""POSIX serial/Hayes connector. Core BBS owns all identities and permissions.

Uses a loopback Telnet listener; no account, password or application data is
persisted here. One process owns one device. Binary payloads are not inspected.
"""
import argparse,contextlib,fcntl,ipaddress,logging,os,select,signal,socket,stat,struct,sys,termios,time

LIMIT=131072

def platform_profile(requested='auto',host=None):
    """Explicit platform selection, never silently apply another OS profile."""
    host=sys.platform if host is None else host
    actual='freebsd' if host.startswith('freebsd') else 'linux' if host.startswith('linux') else None
    if actual is None:raise ValueError('Serial connector supports FreeBSD and Linux only')
    if requested not in ('auto','freebsd','linux'):raise ValueError('Unknown serial platform')
    if requested!='auto' and requested!=actual:raise ValueError('Selected serial platform '+requested+' does not match running OS '+actual)
    return actual

def flow_flag(platform):
    if platform=='freebsd':
        # FreeBSD names the two directions separately on some Python builds.
        receive=getattr(termios,'CRTS_IFLOW',0);send=getattr(termios,'CCTS_OFLOW',0)
        if receive and send:return receive|send
    return getattr(termios,'CRTSCTS',0)

class Telnet:
    def __init__(self):self.mode=0;self.verb=0;self.length=0;self.offered=set()
    def feed(self,raw):
        data=bytearray();answer=bytearray()
        for b in raw:
            if self.mode==0:
                if b==255:self.mode=1
                else:data.append(b)
            elif self.mode==1:
                if b==255:data.append(b);self.mode=0
                elif b in (251,252,253,254):self.verb=b;self.mode=2
                elif b==250:self.mode=3;self.length=0
                else:self.mode=0
            elif self.mode==2:
                key=(self.verb,b)
                if key not in self.offered:
                    self.offered.add(key)
                    if self.verb==251:answer.extend((255,253 if b in (0,1,3) else 254,b))
                    elif self.verb==253:answer.extend((255,251 if b in (0,3) else 252,b))
                self.mode=0
            else:
                self.length+=1
                if self.length>4096:raise ValueError('Oversized Telnet subnegotiation')
                if self.mode==4:self.mode=0 if b==240 else 3
                elif b==255:self.mode=4
        return data,answer

def carrier_present(fd):
    mask=struct.unpack('i',fcntl.ioctl(fd,termios.TIOCMGET,struct.pack('i',0)))[0]
    return bool(mask&termios.TIOCM_CAR)

def dtr(fd,enabled):
    fcntl.ioctl(fd,termios.TIOCMBIS if enabled else termios.TIOCMBIC,struct.pack('i',termios.TIOCM_DTR))

@contextlib.contextmanager
def device(path,baud,flow,platform='auto'):
    platform=platform_profile(platform)
    fd=os.open(path,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK|os.O_NOFOLLOW)
    saved=None;exclusive=False
    try:
        if not stat.S_ISCHR(os.fstat(fd).st_mode):raise ValueError('Serial device must be a character device')
        fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        fcntl.ioctl(fd,termios.TIOCEXCL)
        exclusive=True
        saved=termios.tcgetattr(fd);settings=termios.tcgetattr(fd)
        settings[0]=0;settings[1]=0;settings[2]=termios.CS8|termios.CREAD|termios.CLOCAL;settings[3]=0
        if flow=='rtscts':
            flag=flow_flag(platform)
            if not flag:raise ValueError('RTS/CTS is not supported by this platform')
            settings[2]|=flag
        speed=getattr(termios,'B'+str(baud),None)
        if speed is None:raise ValueError('Unsupported baud rate')
        settings[4]=settings[5]=speed;settings[6][termios.VMIN]=0;settings[6][termios.VTIME]=0
        termios.tcsetattr(fd,termios.TCSANOW,settings)
        termios.tcflush(fd,termios.TCIOFLUSH)
        yield fd
    finally:
        try:
            if saved is not None:termios.tcsetattr(fd,termios.TCSANOW,saved)
        finally:
            try:
                if exclusive:fcntl.ioctl(fd,termios.TIOCNXCL)
            finally:os.close(fd)

def write_all(fd,data,timeout=10):
    end=time.monotonic()+timeout
    while data:
        left=end-time.monotonic()
        if left<=0 or not select.select([],[fd],[],left)[1]:raise TimeoutError('Serial write timeout')
        try:n=os.write(fd,data)
        except BlockingIOError:continue
        if n<=0:raise ConnectionError('Serial write failed')
        data=data[n:]

def modem_answer(fd,initialize,timeout=60):
    """No modem result-code parsing after CONNECT: DCD owns hangup detection."""
    if not initialize or len(initialize)>120 or not initialize.startswith('AT') or any(ord(c)<32 or ord(c)>126 for c in initialize):
        raise ValueError('Use one printable AT initialization command')
    write_all(fd,initialize.encode('ascii')+b'\r')
    line=bytearray();state='initialize';deadline=time.monotonic()+timeout
    while True:
        if state!='ring' and time.monotonic()>=deadline:raise TimeoutError('Modem response timeout')
        if not select.select([fd],[],[],1)[0]:continue
        # One byte prevents discarding terminal input following CONNECT.
        raw=os.read(fd,1)
        if not raw:raise ConnectionError('Serial device closed')
        if raw not in (b'\r',b'\n'):
            line.extend(raw)
            if len(line)>512:raise ValueError('Oversized modem response')
            continue
        text=bytes(line).strip();line.clear()
        if not text:continue
        if state=='initialize' and text==b'OK':state='ring'
        elif state=='ring' and text==b'RING':
            write_all(fd,b'ATA\r');state='answer';deadline=time.monotonic()+timeout
        elif state=='answer' and (text==b'CONNECT' or text.startswith(b'CONNECT ')):return
        elif text in (b'ERROR',b'NO CARRIER',b'BUSY',b'NO DIALTONE'):
            if state=='initialize':raise ConnectionError('Modem initialization rejected')
            state='ring'

def relay(fd,connection,*,carrier=False,timeout=300):
    """Bounded bidirectional buffers and partial writes; no transfer decoding."""
    connection.setblocking(False);connection.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
    terminal=bytearray();network=bytearray();codec=Telnet();last=time.monotonic();eof=False
    while True:
        if carrier and not carrier_present(fd):return 'carrier lost'
        if timeout and time.monotonic()-last>timeout:return 'inactivity timeout'
        readers=[];writers=[]
        if eof and not terminal:return 'BBS closed'
        if not eof and len(network)<LIMIT-8192:readers.append(fd)
        if not eof and len(terminal)<LIMIT-8192 and len(network)<LIMIT-8192:readers.append(connection)
        if terminal:writers.append(fd)
        if network:writers.append(connection)
        readable,writable,_=select.select(readers,writers,[],.25)
        for target in readable:
            try:raw=os.read(fd,4096) if target==fd else connection.recv(4096)
            except BlockingIOError:continue
            if not raw:
                if target==fd:return 'serial closed'
                eof=True;network.clear();continue
            last=time.monotonic()
            if target==fd:network.extend(raw.replace(b'\xff',b'\xff\xff'))
            else:
                data,answer=codec.feed(raw);terminal.extend(data);network.extend(answer)
        for target in writable:
            try:
                if target==fd:n=os.write(fd,terminal);del terminal[:n]
                else:n=connection.send(network);del network[:n]
            except BlockingIOError:continue
            if n<=0:return 'write closed'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--platform',choices=('auto','freebsd','linux'),default='auto',help='Select the FreeBSD or Linux serial profile (default: detect running OS)')
    p.add_argument('--device',required=True);p.add_argument('--host',default='127.0.0.1');p.add_argument('--port',type=int,default=2222)
    p.add_argument('--baud',type=int,default=115200);p.add_argument('--flow',choices=('none','rtscts'),default='rtscts')
    p.add_argument('--mode',choices=('direct','modem'),default='direct');p.add_argument('--init',default='ATE0Q0V1&C1&D2S0=0')
    p.add_argument('--ignore-carrier',action='store_true',help='PTY/test or direct cable only; disables DCD and DTR handling')
    p.add_argument('--timeout',type=int,default=300);p.add_argument('--once',action='store_true')
    a=p.parse_args()
    try:platform=platform_profile(a.platform)
    except ValueError as error:p.error(str(error))
    if not ipaddress.ip_address(a.host).is_loopback:p.error('BBS listener must be loopback')
    if not 1<=a.port<=65535 or a.timeout<0:p.error('Invalid port or timeout')
    logging.basicConfig(level=logging.INFO,format='%(asctime)s %(message)s')
    def stop(*_):raise KeyboardInterrupt
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    try:
        logging.info('Serial platform: %s',platform)
        with device(a.device,a.baud,a.flow,platform) as fd:
            while True:
                try:
                    if not a.ignore_carrier:dtr(fd,True)
                    if a.mode=='modem':modem_answer(fd,a.init)
                    with socket.create_connection((a.host,a.port),timeout=10) as connection:
                        logging.info('Serial session connected')
                        reason=relay(fd,connection,carrier=not a.ignore_carrier,timeout=a.timeout)
                        logging.info('Serial session ended: %s',reason)
                finally:
                    if not a.ignore_carrier:dtr(fd,False)
                    termios.tcflush(fd,termios.TCIOFLUSH)
                if a.once:break
                time.sleep(1)
    except KeyboardInterrupt:return
    except (OSError,ValueError) as error:p.exit(1,str(error)+'\n')

if __name__=='__main__':main()
