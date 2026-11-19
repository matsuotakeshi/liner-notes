#!/usr/bin/env python3
"""Local Door ticket Broker. Candidate; no automatic service activation.

Issuer is the privileged isolation supervisor (UID 0), never a game or the BBS
UID. Its live socket owns authorization lifetime. Consumer UIDs are dedicated
game servers. A TCP listener and unauthenticated protocol are not provided.
"""
import argparse,asyncio,contextlib,ctypes,json,os,signal,socket,stat,struct,sys
from pathlib import Path
from door_broker_core import Broker,Denied,identity,positive

MAX_LINE=4096
WRITE_TIMEOUT=3

def peer_uid(sock):
    if sys.platform.startswith('linux'):
        return struct.unpack('3i',sock.getsockopt(socket.SOL_SOCKET,socket.SO_PEERCRED,12))[1]
    if sys.platform.startswith('freebsd'):
        libc=ctypes.CDLL(None,use_errno=True);uid=ctypes.c_uint();gid=ctypes.c_uint()
        call=libc.getpeereid;call.argtypes=[ctypes.c_int,ctypes.POINTER(ctypes.c_uint),ctypes.POINTER(ctypes.c_uint)];call.restype=ctypes.c_int
        if call(sock.fileno(),ctypes.byref(uid),ctypes.byref(gid))!=0:raise OSError('peer credentials unavailable')
        return uid.value
    if sys.platform.startswith('sunos'):
        libc=ctypes.CDLL(None,use_errno=True);credential=ctypes.c_void_p()
        get=libc.getpeerucred;get.argtypes=[ctypes.c_int,ctypes.POINTER(ctypes.c_void_p)];get.restype=ctypes.c_int
        uid=libc.ucred_geteuid;uid.argtypes=[ctypes.c_void_p];uid.restype=ctypes.c_uint
        free=libc.ucred_free;free.argtypes=[ctypes.c_void_p];free.restype=None
        if get(sock.fileno(),ctypes.byref(credential))!=0:raise OSError('peer credentials unavailable')
        if not credential.value:raise OSError('missing peer credentials')
        try:
            value=uid(credential)
            if value==ctypes.c_uint(-1).value:raise OSError('invalid peer credentials')
            return value
        finally:free(credential)
    raise OSError('peer credentials unsupported on this OS')

def secure_path(path,owner,directory=False):
    path=Path(path)
    if not path.is_absolute():raise ValueError('absolute path required')
    for item in (path,*path.parents):
        st=item.lstat()
        if stat.S_ISLNK(st.st_mode) or st.st_uid not in (0,owner) or st.st_mode&0o022:raise ValueError('unsafe ownership or permissions')
        expected_dir=item!=path or directory
        if expected_dir and not stat.S_ISDIR(st.st_mode):raise ValueError('directory required')
        if not expected_dir and not stat.S_ISREG(st.st_mode):raise ValueError('regular file required')
    return path

def configuration(path):
    source=secure_path(path,0)
    config=json.loads(source.read_text(encoding='utf8'))
    if set(config)!={'bbs','bbs_uid','broker_uid','socket','socket_gid','door_uids','consumers'}:raise ValueError('invalid configuration fields')
    identity(config['bbs']);bbs=positive(config['bbs_uid']);broker=positive(config['broker_uid']);positive(config['socket_gid'])
    if bbs==broker:raise ValueError('BBS and Broker require different UIDs')
    slots=config['door_uids']
    if not isinstance(slots,list) or not slots or len(set(slots))!=len(slots):raise ValueError('distinct Door slot UIDs required')
    slots={positive(uid) for uid in slots}
    consumers={int(uid):doors for uid,doors in config['consumers'].items()}
    if not consumers:raise ValueError('game server consumers required')
    if slots&({bbs,broker}|set(consumers)) or {bbs,broker}&set(consumers):raise ValueError('Door, BBS, Broker and game server UIDs must be disjoint')
    for uid,doors in consumers.items():
        positive(uid)
        if not isinstance(doors,list) or not doors:raise ValueError('consumer doors required')
        for door in doors:positive(door,86400)
    endpoint=Path(config['socket']);secure_path(endpoint.parent,broker,directory=True)
    if len(os.fsencode(endpoint))>100:raise ValueError('socket path too long')
    config['consumers']=consumers
    return config

def check_ready(endpoint,uid):
    # A pathname alone is not readiness: it may be an orphan after a crash.
    st=Path(endpoint).lstat()
    if not stat.S_ISSOCK(st.st_mode) or st.st_uid!=uid:raise ValueError('socket identity')
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as connection:
        connection.settimeout(3);connection.connect(str(endpoint))
        if peer_uid(connection)!=uid:raise ValueError('socket peer identity')


class Server:
    def __init__(self,broker,*,peer=peer_uid):
        self.broker=broker;self.peer=peer;self.clients=set()

    async def handle(self,reader,writer):
        session=None
        try:
            uid=self.peer(writer.get_extra_info('socket'))
            if uid!=0 and uid not in self.broker.consumers:raise Denied('authorization denied')
            if len(self.clients)>=self.broker.capacity:raise Denied('capacity reached')
            self.clients.add(writer)
            while True:
                # The issuer's socket stays open throughout the Door lifetime.
                raw=await asyncio.wait_for(reader.readline(),self.broker.session_ttl if session else 30)
                if not raw:break
                if len(raw)>MAX_LINE or not raw.endswith(b'\n'):raise Denied('invalid request')
                request=json.loads(raw)
                if not isinstance(request,dict):raise Denied('invalid request')
                op=request.get('op')
                if uid==0:
                    if op!='open' or session is not None or set(request)!={'op','member','door','session','generation'}:raise Denied('invalid issuer request')
                    session=self.broker.open(request['member'],request['door'],request['session'],request['generation'])
                    result={'ticket':self.broker.issue(session),'expires_in':self.broker.ttl}
                else:
                    if op not in ('consume','check') or set(request)!={'op','door','bbs','token'}:raise Denied('invalid consumer request')
                    positive(request['door'],86400);identity(request['bbs'])
                    result=(self.broker.consume if op=='consume' else self.broker.check)(uid,request['door'],request['bbs'],request['token'])
                writer.write(json.dumps({'ok':True,'result':result},separators=(',',':')).encode()+b'\n')
                await asyncio.wait_for(writer.drain(),WRITE_TIMEOUT)
                if uid!=0:break
        except (Denied,ValueError,OSError,TimeoutError,asyncio.LimitOverrunError):
            # No exception text, request body, identity or token is logged/reflected.
            # Revoke before attempting to notify a peer that may not read.
            if session:self.broker.close(session)
            with contextlib.suppress(OSError,ConnectionError,TimeoutError):
                writer.write(b'{"ok":false,"error":"authorization denied"}\n')
                await asyncio.wait_for(writer.drain(),WRITE_TIMEOUT)
        finally:
            if session:self.broker.close(session)
            self.clients.discard(writer);writer.close()
            try:await asyncio.wait_for(writer.wait_closed(),WRITE_TIMEOUT)
            except (OSError,TimeoutError):writer.transport.abort()

async def serve(config):
    if os.getuid()!=config['broker_uid'] or os.geteuid()!=config['broker_uid']:raise ValueError('run as dedicated Broker UID')
    endpoint=Path(config['socket'])
    if endpoint.exists() or endpoint.is_symlink():raise ValueError('socket exists; verify previous Broker is stopped')
    server=Server(Broker(config['bbs'],config['consumers']))
    old=os.umask(0o077)
    try:listener=await asyncio.start_unix_server(server.handle,path=str(endpoint),limit=MAX_LINE)
    finally:os.umask(old)
    inode=endpoint.lstat().st_ino
    try:
        os.chown(endpoint,-1,config['socket_gid']);os.chmod(endpoint,0o660)
        stop=asyncio.Event();loop=asyncio.get_running_loop()
        for sig in (signal.SIGTERM,signal.SIGINT):loop.add_signal_handler(sig,stop.set)
        async with listener:await stop.wait()
    finally:
        for writer in list(server.clients):writer.close()
        if endpoint.exists() and endpoint.lstat().st_ino==inode:endpoint.unlink()

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--config',required=True)
    checks=parser.add_mutually_exclusive_group()
    checks.add_argument('--check-config',action='store_true',help='validate policy without starting or changing files')
    checks.add_argument('--check-ready',action='store_true',help='probe local socket and verify service UID')
    args=parser.parse_args()
    import resource
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    try:
        config=configuration(args.config)
        if args.check_ready:check_ready(config['socket'],config['broker_uid'])
        elif not args.check_config:asyncio.run(serve(config))
    except KeyboardInterrupt:pass
    except (ValueError,OSError,KeyError,TypeError):parser.exit(1,'Broker startup rejected; check configuration, identity and permissions.\n')

if __name__=='__main__':main()
