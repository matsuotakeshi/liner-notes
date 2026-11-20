#!/usr/bin/env python3
"""Opt-in Linux/FreeBSD Door isolation supervisor; privileged local socket only.

Fixed root-owned policy selects binaries and dedicated slot UIDs. The BBS never
supplies an executable or working directory. No privilege-changing executable
is installed setuid. Solaris remains disabled pending contract-based process isolation.
"""
import argparse,array,contextlib,errno,fcntl,json,os,pty,resource,secrets,select,signal,socket,stat,struct,sys,time
from pathlib import Path
from door_broker import peer_uid,secure_path,check_ready,configuration as broker_configuration
from door_broker_core import identity,positive,auth_generation
from door_processes import Scope, processes, supported, no_new_privileges

class Rejected(ValueError):pass


def failure_event(event,phase,error):
    # Fixed call-site labels, exception class and numeric errno only. Never log
    # str(error), traceback locals, request data, tickets or configured paths.
    number=getattr(error,'errno',None)
    if not isinstance(number,int):number=None
    try:
        print(json.dumps(dict(event=event,phase=phase,error=type(error).__name__,errno=number)),file=sys.stderr,flush=True)
    except Exception:
        pass # failed diagnostics must not prevent revocation/cleanup or ERR


def parent_event(event,*,reason=None,worker=None,status=None,active=None,error=None):
    if os.environ.get('LINER_DOOR_DIAGNOSTICS')!='1':return
    fields={'event':event}
    if reason is not None:fields['reason']=reason # fixed call-site label
    for key,value in (('worker',worker),('status',status),('active',active),('errno',error)):
        if isinstance(value,int):fields[key]=value
    with contextlib.suppress(Exception):
        print(json.dumps(fields),file=sys.stderr,flush=True)

def read_line(sock,timeout=3):
    deadline=time.monotonic()+timeout
    data=bytearray()
    while len(data)<=4096:
        remaining=deadline-time.monotonic()
        if remaining<=0:raise TimeoutError('request deadline')
        sock.settimeout(remaining)
        b=sock.recv(1)
        if not b:raise Rejected('closed request')
        if b==b'\n':return json.loads(data)
        data+=b
    raise Rejected('request too long')

def send(sock,value):sock.sendall(json.dumps(value,separators=(',',':')).encode()+b'\n')

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
        # A writable game directory is intentional; writable ancestors or a
        # symlink controlled by another identity are not an isolation boundary.
        secure_path(slot['cwd'],uid,True)
        if not os.access(slot['exe'],os.X_OK):raise Rejected('slot path')
    if used!=set(policy['door_uids']):raise Rejected('Broker slot identities differ')
    if used&set(policy['consumers']):raise Rejected('consumer/slot identity collision')
    return config

def broker_open(config,request,slot):
    endpoint=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);endpoint.settimeout(3)
    try:
        endpoint.connect(config['broker_socket'])
        if peer_uid(endpoint)!=config['broker_uid']:raise Rejected('Broker identity')
        send(endpoint,dict(op='open_scoped',member=request['user'],door=request['door'],
                           session=request['session'],generation=request['generation'],
                           scope=config['_incarnation']+'.'+str(slot['uid']),
                           sequence=config['_launch_sequence']))
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
        try:
            # Read reservation only after taking the lock; an earlier fstat can
            # predate another worker's crash and otherwise reuse a dirty slot.
            if os.fstat(fd).st_size or processes(slot['uid']):os.close(fd);continue
            os.write(fd,b'BUSY\n');os.fsync(fd)
            return slot,fd
        except BaseException:os.close(fd);raise
    raise Rejected('no isolated slot')

def supervise(config,connection,parent_watch=None):
    slot=None;lock=None;issuer=None;master=None;slave=None;ticket_read=None;pid=None;run=None;scope=None;phase="request"
    try:
        connection.settimeout(3);request=read_line(connection);name=request_check(request)
        phase='slot'
        slot,lock=slot_acquire(config,request['door'],request['level'])
        # This supervisor is trusted, single-threaded, and owns one unique slot UID.
        phase='scope';scope=Scope(slot['uid'])
        token=None
        phase='broker'
        if slot['auth']:issuer,token=broker_open(config,request,slot)
        phase='ticket-pipe'
        if token:
            ticket_read,ticket_write=os.pipe();os.write(ticket_write,(token+'\n').encode());os.close(ticket_write)
        if slot['drop']:
            phase='dropfile'
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
                os.setsid();signal.signal(signal.SIGTERM,signal.SIG_DFL);signal.signal(signal.SIGINT,signal.SIG_DFL);import termios
                fcntl.ioctl(slave,termios.TIOCSCTTY,0)
                for fd in (0,1,2):os.dup2(slave,fd)
                if ticket_read is not None:os.dup2(ticket_read,3);os.set_inheritable(3,True)
                os.closerange(4 if token else 3,resource.getrlimit(resource.RLIMIT_NOFILE)[0])
                os.setgroups([]);os.setgid(slot['gid']);os.setuid(slot['uid'])
                if os.getuid()!=slot['uid'] or os.geteuid()!=slot['uid']:os._exit(126)
                # Prevent a Door from acquiring a different UID through setuid exec.
                no_new_privileges()
                resource.setrlimit(resource.RLIMIT_CORE,(0,0));resource.setrlimit(resource.RLIMIT_NOFILE,(64,64));resource.setrlimit(resource.RLIMIT_CPU,(slot['seconds'],slot['seconds']+1))
                os.chdir(slot['cwd']);os.execve(slot['exe'],[slot['exe']],env)
            except BaseException:os._exit(126)
        os.close(slave);slave=None
        if ticket_read is not None:os.close(ticket_read);ticket_read=None
        phase='handoff'
        connection.sendmsg([b'OK\n'],[(socket.SOL_SOCKET,socket.SCM_RIGHTS,array.array('i',[master]))]);os.close(master);master=None
        phase='running'
        connection.settimeout(None);until=time.monotonic()+slot['seconds']
        while time.monotonic()<until:
            sockets=[connection]+([issuer] if issuer else [])+([parent_watch] if parent_watch is not None else [])
            readable,_,_=select.select(sockets,[],[],0.1)
            if readable:break # EOF, STOP, unexpected data or Broker death all revoke
            done,status=os.waitpid(pid,os.WNOHANG)
            if done:
                if status:print(json.dumps(dict(event='door-exit',status=os.waitstatus_to_exitcode(status))),file=sys.stderr,flush=True)
                pid=None;break
    except (OSError,ValueError,KeyError,TypeError) as error:
        failure_event('launch-rejected',phase,error)
        with contextlib.suppress(OSError):connection.sendall(b'ERR\n')
    except BaseException as error:
        failure_event('launch-failed',phase,error)
        raise
    finally:
        cleanup_phase='revoke'
        try:
            if issuer:issuer.close() # invalidate even consumed leases before cleanup
            cleanup_phase='descriptors'
            for fd in (master,slave,ticket_read):
                if fd is not None:os.close(fd)
            cleanup_phase='scope-stop'
            if scope:scope.stop()
            elif slot:raise Rejected('Cleanup scope unavailable; slot stays reserved')
            cleanup_phase='dropfile-remove'
            if run:
                with contextlib.suppress(OSError):(run/'DORINFO1.DEF').unlink();run.rmdir()
            cleanup_phase='slot-release'
            if lock is not None:os.ftruncate(lock,0);os.fsync(lock);os.close(lock)
            cleanup_phase='connection-close'
            with contextlib.suppress(OSError):connection.sendall(b'DONE\n')
            connection.close()
        except BaseException as error:
            failure_event('cleanup-failed',cleanup_phase,error)
            raise

def termios_TIOCSWINSZ():
    import termios
    return termios.TIOCSWINSZ

def accept_connection(listener):
    try:return listener.accept()[0]
    except socket.timeout:return None
    except OSError as error:
        if error.errno not in (errno.EMFILE,errno.ENFILE,errno.ENOBUFS,errno.ENOMEM):raise
        # Retain live workers and back off instead of spinning or revoking
        # unrelated games when admission resources run out.
        time.sleep(.1)
        return None


def start_worker(config,connection,listener,watch,alive,children):
    # Reject only this request on credential/fork failure. Existing worker
    # watch pipes and reservations belong to already-running Doors.
    phase='credentials'
    try:
        if peer_uid(connection)!=config['bbs_uid']:
            parent_event('admission-rejected',reason='peer',active=len(children));return
        if len(children)>=len(config['slots'])+8:
            parent_event('admission-rejected',reason='capacity',active=len(children));return
        # Allocate before fork in the single-threaded parent; callers cannot
        # choose/replay these fields. Gaps after failed forks are intentional.
        config.setdefault('_incarnation',secrets.token_hex(16))
        config['_launch_sequence']=positive(config.get('_launch_sequence',0)+1)
        phase='fork'
        pid=os.fork()
    except (OSError,ValueError) as error:
        parent_event('admission-rejected',reason=phase,active=len(children),error=getattr(error,'errno',None))
        print('{"event":"admission-unavailable"}',file=sys.stderr,flush=True)
        return
    if pid==0:
        status=0;phase='setup'
        try:
            listener.close();os.close(alive)
            signal.signal(signal.SIGTERM,signal.SIG_IGN);signal.signal(signal.SIGINT,signal.SIG_IGN)
            phase='supervise';supervise(config,connection,watch)
        except BaseException as error:
            status=1;failure_event('worker-failed',phase,error)
        finally:os._exit(status)
    children.add(pid)
    parent_event('worker-accepted',worker=pid,active=len(children))


def reap_workers(children):
    for child in list(children):
        done,status=os.waitpid(child,os.WNOHANG)
        if done:
            children.remove(child)
            result=os.waitstatus_to_exitcode(status)
            parent_event('worker-reaped',worker=child,status=result,active=len(children))
            if status:
                with contextlib.suppress(Exception):
                    print(json.dumps(dict(event='worker-exit',status=result)),file=sys.stderr,flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--config',required=True)
    checks=parser.add_mutually_exclusive_group()
    checks.add_argument('--check-config',action='store_true',help='validate policy without starting or changing files')
    checks.add_argument('--check-ready',action='store_true',help='probe local socket and verify supervisor UID')
    args=parser.parse_args()
    if not supported() or os.getuid()!=0:parser.exit(1,'Isolation requires root and a supported Linux/FreeBSD process backend.\n')
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    try:
        config=config_read(args.config);endpoint=Path(config['socket'])
        if args.check_ready:check_ready(endpoint,0);return
        if args.check_config:return
    except (ValueError,OSError,KeyError,TypeError):parser.exit(1,'Isolation policy or service rejected.\n')
    if endpoint.exists() or endpoint.is_symlink():parser.exit(1,'Socket exists; verify the old supervisor is stopped.\n')
    listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);old=os.umask(0o077)
    try:listener.bind(str(endpoint))
    finally:os.umask(old)
    inode=endpoint.lstat().st_ino
    os.chown(endpoint,0,config['socket_gid']);os.chmod(endpoint,0o660);listener.listen(32);listener.settimeout(.25)
    watch,alive=os.pipe();children=set();stopping=False
    def stop(signum,frame):
        nonlocal stopping
        stopping=True
    for sig in (signal.SIGTERM,signal.SIGINT):signal.signal(sig,stop)
    try:
        while not stopping:
            reap_workers(children)
            connection=accept_connection(listener)
            if connection is None:continue
            try:
                start_worker(config,connection,listener,watch,alive,children)
            finally:connection.close()
    finally:
        listener.close();os.close(alive);os.close(watch)
        if endpoint.exists() and endpoint.lstat().st_ino==inode:endpoint.unlink()
        # Parent death also closes alive, revoking every worker. Do not forcibly
        # kill a worker and silently make its slot reusable after partial cleanup.
        while children:reap_workers(children);time.sleep(.05)

if __name__=='__main__':main()
