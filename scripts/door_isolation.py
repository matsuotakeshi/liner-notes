#!/usr/bin/env python3
"""Opt-in Linux Door isolation supervisor; privileged local socket only.

Fixed root-owned policy selects binaries and dedicated slot UIDs. The BBS never
supplies an executable or working directory. No privilege-changing executable
is installed setuid. FreeBSD/Solaris backends are deliberately unavailable here.
"""
import argparse,array,contextlib,ctypes,fcntl,json,os,pty,resource,secrets,select,signal,socket,stat,struct,sys,time
from pathlib import Path
from door_broker import peer_uid,secure_path,configuration as broker_configuration
from door_broker_core import identity,positive,auth_generation

class Rejected(ValueError):pass

def read_line(sock):
    data=bytearray()
    while len(data)<=4096:
        b=sock.recv(1)
        if not b:raise Rejected('closed request')
        if b==b'\n':return json.loads(data)
        data+=b
    raise Rejected('request too long')

def send(sock,value):sock.sendall(json.dumps(value,separators=(',',':')).encode()+b'\n')

def processes(uid,terminate=False,sig=signal.SIGKILL):
    """Pin each Linux task before checking UID/signalling: no recycled-PID kill."""
    active=False
    for entry in Path('/proc').iterdir():
        if not entry.name.isdecimal():continue
        handle=None
        try:
            handle=os.pidfd_open(int(entry.name))
            status=(entry/'status').read_text()
            values=next(line for line in status.splitlines() if line.startswith('Uid:')).split()[1:]
            if uid not in map(int,values):continue
            # Zombies cannot run/fork/read credentials and must not reserve a slot forever.
            if next(line for line in status.splitlines() if line.startswith('State:')).split()[1]=='Z':continue
            active=True
            if terminate:signal.pidfd_send_signal(handle,sig)
        except (ProcessLookupError,FileNotFoundError):pass
        finally:
            if handle is not None:os.close(handle)
    return active

def config_read(path):
    config=json.loads(secure_path(path,0).read_text(encoding='utf8'))
    if set(config)!={'bbs_uid','socket','socket_gid','broker_config','state_dir','slots'}:raise Rejected('policy fields')
    policy=broker_configuration(config['broker_config'])
    bbs=positive(config['bbs_uid']);broker=policy['broker_uid'];positive(config['socket_gid'])
    if bbs!=policy['bbs_uid']:raise Rejected('BBS identity mismatch')
    config['broker_uid']=broker;config['broker_socket']=policy['socket']
    if bbs==broker:raise Rejected('UID collision')
    secure_path(Path(config['socket']).parent,0,True);secure_path(config['state_dir'],0,True)
    used=set();pairs=set()
    if not config['slots'] or len(config['slots'])>128:raise Rejected('slot count')
    for slot in config['slots']:
        if set(slot)!={'door','node','uid','gid','exe','cwd','level','seconds','drop','auth'}:raise Rejected('slot fields')
        positive(slot['door'],86400);positive(slot['node'],128);uid=positive(slot['uid']);positive(slot['gid']);positive(slot['seconds'],86400)
        if type(slot['level']) is not int or not 0<=slot['level']<=5 or type(slot['drop']) is not bool or type(slot['auth']) is not bool:raise Rejected('slot values')
        if slot['gid']!=uid:raise Rejected('dedicated slot GID must match UID')
        if uid in used or uid in (bbs,broker) or (slot['door'],slot['node']) in pairs:raise Rejected('slot isolation')
        used.add(uid);pairs.add((slot['door'],slot['node']))
        secure_path(slot['exe'],0)
        if not os.access(slot['exe'],os.X_OK) or not Path(slot['cwd']).is_absolute() or not Path(slot['cwd']).is_dir():raise Rejected('slot path')
    if used!=set(policy['door_uids']):raise Rejected('Broker slot identities differ')
    if used&set(policy['consumers']):raise Rejected('consumer/slot identity collision')
    return config

def broker_open(config,request):
    endpoint=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);endpoint.settimeout(3)
    try:
        endpoint.connect(config['broker_socket'])
        if peer_uid(endpoint)!=config['broker_uid']:raise Rejected('Broker identity')
        send(endpoint,dict(op='open',member=request['user'],door=request['door'],session=request['session'],generation=request['generation']))
        result=read_line(endpoint)
        if not result.get('ok'):raise Rejected('Broker rejected')
        token=result['result']['ticket']
        if len(token)!=64 or any(c not in '0123456789abcdef' for c in token):raise Rejected('Broker response')
        endpoint.settimeout(None);return endpoint,token
    except BaseException:endpoint.close();raise

def request_check(request):
    if not isinstance(request,dict) or set(request)!={'door','user','level','columns','rows','namehex','session','generation'}:raise Rejected('request fields')
    positive(request['door'],86400);positive(request['user'],999999);auth_generation(request['generation']);identity(request['session'])
    if type(request['level']) is not int or not 0<=request['level']<=5:raise Rejected('level')
    if type(request['columns']) is not int or not 20<=request['columns']<=240 or type(request['rows']) is not int or not 5<=request['rows']<=100:raise Rejected('dimensions')
    name=bytes.fromhex(request['namehex']).decode('utf8')
    if not name or len(name.encode())>32 or any(ord(c)<32 or ord(c)==127 for c in name):raise Rejected('name')
    return name

def slot_acquire(config,door,level):
    for slot in config['slots']:
        if slot['door']!=door or level<slot['level']:continue
        fd=os.open(str(Path(config['state_dir'])/('slot-'+str(slot['uid']))),os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW,0o600)
        st=os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_uid!=0 or st.st_nlink!=1 or st.st_mode&0o077:os.close(fd);raise Rejected('slot lock')
        try:fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:os.close(fd);continue
        if processes(slot['uid']):os.close(fd);continue
        return slot,fd
    raise Rejected('no isolated slot')

def supervise(config,connection):
    slot=None;lock=None;issuer=None;master=None;slave=None;ticket_read=None;pid=None;run=None;phase="request"
    try:
        connection.settimeout(3);request=read_line(connection);name=request_check(request)
        phase='slot'
        slot,lock=slot_acquire(config,request['door'],request['level'])
        # This supervisor is trusted, single-threaded, and owns one unique slot UID.
        if ctypes.CDLL(None,use_errno=True).prctl(36,1,0,0,0)!=0:raise Rejected('subreaper unavailable')
        token=None
        phase='broker'
        if slot['auth']:issuer,token=broker_open(config,request)
        if token:
            ticket_read,ticket_write=os.pipe();os.write(ticket_write,(token+'\n').encode());os.close(ticket_write)
        if slot['drop']:
            run=Path(config['state_dir'])/('run-'+secrets.token_hex(16));run.mkdir(mode=0o700)
            drop=run/'DORINFO1.DEF'
            drop.write_text('Liner notes\r\nSYSOP\r\n\r\nCOM0\r\n0 BAUD,N,8,1\r\n0\r\nLN%09d\r\n\r\n\r\n1\r\n%d\r\n%d\r\n'%(request['user'],request['level'],(slot['seconds']+59)//60),newline='')
            os.chmod(drop,0o600);os.chown(drop,slot['uid'],slot['gid']);os.chown(run,slot['uid'],slot['gid'])
        phase='pty'
        master,slave=pty.openpty();fcntl.ioctl(slave,termios_TIOCSWINSZ(),struct.pack('HHHH',request['rows'],request['columns'],0,0))
        env={'PATH':'/usr/local/bin:/usr/bin:/bin','TERM':'xterm','LANG':'C.UTF-8','HOME':slot['cwd'],
             'LINER_USER_ID':str(request['user']),'LINER_USER':name,'LINER_DOOR_ID':str(slot['door']),
             'LINER_DOOR_NODE':str(slot['node']),'LINER_DOOR_DROP_DIR':str(run or ''),
             'COLUMNS':str(request['columns']),'LINES':str(request['rows'])}
        if token:env['LINER_AUTH_FD']='3' # descriptor number only, never the credential
        phase='launch'
        pid=os.fork()
        if pid==0:
            try:
                os.setsid();import termios
                fcntl.ioctl(slave,termios.TIOCSCTTY,0)
                for fd in (0,1,2):os.dup2(slave,fd)
                if ticket_read is not None:os.dup2(ticket_read,3);os.set_inheritable(3,True)
                os.closerange(4 if token else 3,resource.getrlimit(resource.RLIMIT_NOFILE)[0])
                os.setgroups([]);os.setgid(slot['gid']);os.setuid(slot['uid'])
                if os.getuid()!=slot['uid'] or os.geteuid()!=slot['uid']:os._exit(126)
                # Prevent a Door from acquiring a different UID through setuid exec.
                if ctypes.CDLL(None,use_errno=True).prctl(38,1,0,0,0)!=0:os._exit(126)
                resource.setrlimit(resource.RLIMIT_CORE,(0,0));resource.setrlimit(resource.RLIMIT_NOFILE,(64,64));resource.setrlimit(resource.RLIMIT_CPU,(slot['seconds'],slot['seconds']+1))
                os.chdir(slot['cwd']);os.execve(slot['exe'],[slot['exe']],env)
            except BaseException:os._exit(126)
        os.close(slave);slave=None
        if ticket_read is not None:os.close(ticket_read);ticket_read=None
        connection.sendmsg([b'OK\n'],[(socket.SOL_SOCKET,socket.SCM_RIGHTS,array.array('i',[master]))]);os.close(master);master=None
        connection.settimeout(None);until=time.monotonic()+slot['seconds']
        while time.monotonic()<until:
            sockets=[connection]+([issuer] if issuer else [])
            readable,_,_=select.select(sockets,[],[],0.1)
            if readable:break # EOF, STOP, unexpected data or Broker death all revoke
            done,status=os.waitpid(pid,os.WNOHANG)
            if done:
                if status:print(json.dumps(dict(event='door-exit',status=os.waitstatus_to_exitcode(status))),file=sys.stderr,flush=True)
                pid=None;break
    except (OSError,ValueError,KeyError,TypeError) as error:
        print(json.dumps(dict(event='launch-rejected',phase=phase,error=type(error).__name__,errno=getattr(error,'errno',None))),file=sys.stderr,flush=True)
        with contextlib.suppress(OSError):connection.sendall(b'ERR\n')
    finally:
        if issuer:issuer.close() # invalidate even consumed leases before cleanup
        for fd in (master,slave,ticket_read):
            if fd is not None:os.close(fd)
        if slot:
            if processes(slot['uid'],terminate=True,sig=signal.SIGHUP):time.sleep(0.25)
            # Kill all tasks under the dedicated UID, including setsid/double forks.
            # Keep the lease if cleanup is incomplete; never recycle it to a user.
            while processes(slot['uid'],terminate=True):time.sleep(0.05)
            while True:
                try:
                    child,_=os.waitpid(-1,os.WNOHANG)
                    if child==0:time.sleep(0.02);continue
                except ChildProcessError:break
        if run:
            with contextlib.suppress(OSError):(run/'DORINFO1.DEF').unlink();run.rmdir()
        if lock is not None:os.close(lock)
        with contextlib.suppress(OSError):connection.sendall(b'DONE\n')
        connection.close()

def termios_TIOCSWINSZ():
    import termios
    return termios.TIOCSWINSZ

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--config',required=True);args=parser.parse_args()
    if not sys.platform.startswith('linux') or os.getuid()!=0 or not hasattr(os,'pidfd_open') or not hasattr(signal,'pidfd_send_signal'):parser.exit(1,'Isolation supervisor currently requires Linux, root and pidfd support.\n')
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    config=config_read(args.config);endpoint=Path(config['socket'])
    if endpoint.exists() or endpoint.is_symlink():parser.exit(1,'Socket exists; verify the old supervisor is stopped.\n')
    listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);old=os.umask(0o077)
    try:listener.bind(str(endpoint))
    finally:os.umask(old)
    os.chown(endpoint,0,config['socket_gid']);os.chmod(endpoint,0o660);listener.listen(32)
    signal.signal(signal.SIGCHLD,signal.SIG_IGN)
    try:
        while True:
            connection,_=listener.accept()
            if peer_uid(connection)!=config['bbs_uid']:connection.close();continue
            pid=os.fork()
            if pid==0:
                listener.close();signal.signal(signal.SIGCHLD,signal.SIG_DFL)
                try:supervise(config,connection)
                finally:os._exit(0)
            connection.close()
    finally:listener.close();endpoint.unlink(missing_ok=True)

if __name__=='__main__':main()
