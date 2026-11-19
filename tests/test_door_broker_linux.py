"""Root-only test in a hosted Linux container or an admitted disposable FreeBSD jail."""
from pathlib import Path
import contextlib,json,multiprocessing,os,re,shutil,signal,socket,sqlite3,subprocess,sys,tempfile,time
from test_board import Server
from test_telnet import Client

ROOT=Path(__file__).resolve().parents[1]
BBS,BROKER,GAME,SLOT1,SLOT2=64501,64502,64503,64504,64505

def identity(uid,groups=()):
    def apply():os.setgroups(list(groups));os.setgid(uid);os.setuid(uid)
    return apply

def ready(path,process):
    for _ in range(100):
        if Path(path).exists():return
        if process.poll() is not None:raise AssertionError(('service exited during startup',process.stderr.read(2048)))
        time.sleep(.05)
    raise AssertionError('service startup timeout')

def game_server(path,broker,scripts):
    identity(GAME)();os.umask(0o077);sys.path.insert(0,str(scripts))
    from door_auth_client import DoorAuthorization,AuthorizationError
    from door_broker import peer_uid
    db=sqlite3.connect(path.parent/'players.sqlite')
    db.execute('CREATE TABLE IF NOT EXISTS players (member INTEGER PRIMARY KEY, credits INTEGER NOT NULL)');db.commit()
    sock=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);sock.bind(str(path));os.chmod(path,0o666);sock.listen(10)
    leases={}
    while True:
        c,_=sock.accept()
        with c:
            c.settimeout(3);uid=peer_uid(c)
            raw=c.makefile('rb').readline(4096);data=json.loads(raw)
            try:
                if uid==0:
                    active=0
                    for lease in leases.values():
                        try:lease.authorize();active+=1
                        except AuthorizationError:pass
                    answer={'active':active,'players':list(db.execute('SELECT member,credits FROM players ORDER BY member'))}
                elif uid not in (SLOT1,SLOT2):answer={'denied':True}
                elif 'ticket' in data:
                    lease=DoorAuthorization(str(broker),bbs='fixture',door=1,ticket=data['ticket'],broker_uid=BROKER)
                    answer=lease.authorize();leases[uid]=lease
                else:
                    lease=leases.get(uid)
                    if lease is None:raise AuthorizationError('login required')
                    member=lease.authorize(data['member'])['member']
                    db.execute('INSERT INTO players VALUES (?,1) ON CONFLICT(member) DO UPDATE SET credits=credits+1',(member,));db.commit()
                    answer={'saved':member}
            except AuthorizationError:answer={'denied':True}
            c.sendall(json.dumps(answer).encode()+b'\n')

def query(path,value):
    with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as c:
        c.settimeout(3);c.connect(str(path));c.sendall(json.dumps(value).encode()+b'\n');return json.loads(c.makefile('rb').readline(4096))

def run(binary):
    assert os.geteuid()==0,'isolated root fixture required'
    if sys.platform.startswith('freebsd'):
        assert subprocess.check_output(['/sbin/sysctl','-n','security.jail.jailed']).strip()==b'1','disposable jail required'
        assert subprocess.check_output(['/bin/hostname']).strip()==b'liner-ci.invalid','CI jail required'
    else:assert sys.platform.startswith('linux'),'unsupported isolation backend'
    # Dedicated IDs must be unused; refuse collisions rather than kill other tests.
    sys.path.insert(0,str(ROOT/'scripts'))
    from door_isolation import processes
    for uid in (BBS,BROKER,GAME,SLOT1,SLOT2):assert not processes(uid),'fixture UID already active'
    with tempfile.TemporaryDirectory(prefix='ln-broker-',dir='/var/run' if sys.platform.startswith('freebsd') else '/run') as tmp:
        p=Path(tmp);p.chmod(0o755);scripts=p/'scripts';scripts.mkdir()
        for name in ('door_broker.py','door_broker_core.py','door_isolation.py','door_auth_client.py','door_processes.py'):shutil.copy2(ROOT/'scripts'/name,scripts/name)
        runtime=p/'runtime';shutil.copy2(binary,runtime);runtime.chmod(0o755)
        data=p/'data';data.mkdir(mode=0o700);os.chown(data,BBS,BBS)
        (data/'private').write_text('private BBS fixture');os.chown(data/'private',BBS,BBS);(data/'private').chmod(0o600)
        broker_dir=p/'broker';broker_dir.mkdir(mode=0o755);os.chown(broker_dir,BROKER,BROKER)
        state=p/'state';state.mkdir(mode=0o711)
        game_dir=p/'server';game_dir.mkdir(mode=0o711);os.chown(game_dir,GAME,GAME)
        endpoint=broker_dir/'broker.sock';game_socket=game_dir/'game.sock'
        broker_conf=p/'broker.json';broker_conf.write_text(json.dumps(dict(bbs='fixture',bbs_uid=BBS,broker_uid=BROKER,socket=str(endpoint),socket_gid=GAME,door_uids=[SLOT1,SLOT2],consumers={str(GAME):[1]})))
        wrapper=p/'game';wrapper.write_text('#!'+sys.executable+'\n'+r"""
import json,os,socket,sys,tty,signal,time
from pathlib import Path
tty.setraw(0)
fd=int(os.environ['LINER_AUTH_FD']);token=os.read(fd,65).decode().strip();os.close(fd)
with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as c:
    c.connect(GAME_SOCKET);c.sendall(json.dumps({'ticket':token}).encode()+b'\n');claims=json.loads(c.makefile('rb').readline(4096))
assert claims['member']==int(os.environ['LINER_USER_ID'])
assert 'ticket' not in str(os.environ).lower()
assert Path(os.environ['LINER_DOOR_DROP_DIR'],'DORINFO1.DEF').is_file()
try:Path(PRIVATE_PATH).read_bytes()
except PermissionError:pass
else:raise AssertionError('Door read private BBS data')
assert set(os.getgroups()) <= {os.getegid()}, 'supplementary groups inherited'
print('AUTH '+str(claims['member'])+' UID '+str(os.getuid())+'\r\nKEY> ',end='',flush=True)
while True:
    key=os.read(0,1)
    if key==b'q':break
    if key==b's':
        size=os.get_terminal_size(0);print('SIZE '+str(size.columns)+' '+str(size.lines)+'\r\nKEY> ',end='',flush=True)
    if key in (b'g',b'x',b'r'):
        request={'ticket':token} if key==b'r' else {'member':claims['member'] if key==b'g' else 999}
        with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as c:
            c.connect(GAME_SOCKET);c.sendall(json.dumps(request).encode()+b'\n');answer=json.loads(c.makefile('rb').readline(4096))
        print(('DENIED' if answer.get('denied') else 'SAVED')+'\r\nKEY> ',end='',flush=True)
    if key==b'f':
        child=os.fork()
        if child==0:
            os.setsid();signal.signal(signal.SIGHUP,signal.SIG_IGN)
            while True:time.sleep(1)
        print('CHILD '+str(child)+'\r\nKEY> ',end='',flush=True)
""".replace('GAME_SOCKET',repr(str(game_socket))).replace('PRIVATE_PATH',repr(str(data/'private'))));wrapper.chmod(0o755)
        slots=[]
        for node,uid in enumerate((SLOT1,SLOT2),1):
            cwd=p/('slot'+str(node));cwd.mkdir();os.chown(cwd,uid,uid)
            slots.append(dict(door=1,node=node,uid=uid,gid=uid,exe=str(wrapper),cwd=str(cwd),level=0,seconds=30,drop=True,auth=True))
        isolation=p/'isolation.sock';launch_conf=p/'isolation.json'
        launch_conf.write_text(json.dumps(dict(bbs_uid=BBS,socket=str(isolation),socket_gid=BBS,broker_config=str(broker_conf),state_dir=str(state),slots=slots)))
        doors=p/'doors.conf';doors.write_text(f'1|0|30|{wrapper}|{p}|Authenticated fixture|dorinfo1:2\n')
        commands=[([sys.executable,str(scripts/'door_broker.py'),'--config',str(broker_conf)],identity(BROKER,[GAME])),([sys.executable,str(scripts/'door_isolation.py'),'--config',str(launch_conf)],None)]
        services=[];clients=[];bbs=None;game=None
        try:
            for args,pre in commands:
                services.append(subprocess.Popen(args,preexec_fn=pre,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE))
            ready(endpoint,services[0]);ready(isolation,services[1])
            game=multiprocessing.Process(target=game_server,args=(game_socket,endpoint,scripts));game.start()
            for _ in range(100):
                if game_socket.exists():break
                time.sleep(.03)
            assert game_socket.exists()
            bbs=Server(str(runtime),data/'notes',preexec_fn=identity(BBS),members=data/'members',menu=True,telnet=True,language='en',options=['--doors',str(doors),'--door-isolation-socket',str(isolation)])
            for name,member,uid in [('alice',1,SLOT1),('bob',2,SLOT2)]:
                c=Client(bbs.port);clients.append(c);c.until();c.send(('REGISTER '+name+'\r\nfixture-password\r\n').encode());assert b'Logged in' in c.until()
                c.cmd('GAME',b'0:main] > ')
                try:out=c.cmd('1',b'KEY> ')
                except (TimeoutError,AssertionError) as error:raise AssertionError(('isolated Door launch',c.buf[-2000:])) from error
                assert f'AUTH {member} UID {uid}'.encode() in out,out
            assert query(game_socket,{})['active']==2
            # Transfer PTY resize and enforce game-side authority on each mutation.
            clients[0].send(bytes([255,250,31,0,100,0,30,255,240]))
            for _ in range(20):
                clients[0].send(b's');out=clients[0].until(b'KEY> ')
                if b'SIZE 100 30' in out:break
                time.sleep(.05)
            assert b'SIZE 100 30' in out,out
            for client in clients:
                client.send(b'g');assert b'SAVED' in client.until(b'KEY> ')
                for key in (b'x',b'r'):
                    client.send(key);assert b'DENIED' in client.until(b'KEY> ')
            assert query(game_socket,{})['players']==[[1,1],[2,1]]
            clients[0].send(b'f');out=clients[0].until(b'KEY> ');child=int(re.search(rb'CHILD (\d+)',out)[1]);clients[0].close()
            for _ in range(100):
                if not processes(SLOT1):break
                time.sleep(.03)
            assert not processes(SLOT1),'detached child survived'
            assert query(game_socket,{})['active']==1
            clients[1].send(b'q');clients[1].until(b'0:main] > ')
            for _ in range(50):
                if query(game_socket,{})['active']==0:break
                time.sleep(.03)
            assert query(game_socket,{})['active']==0
            # Game server restart preserves data, but cannot keep cached authority.
            game.terminate();game.join(5);assert not game.is_alive();game_socket.unlink()
            game=multiprocessing.Process(target=game_server,args=(game_socket,endpoint,scripts));game.start()
            for _ in range(100):
                if game_socket.exists():break
                time.sleep(.03)
            assert query(game_socket,{})=={'active':0,'players':[[1,1],[2,1]]}
            out=clients[1].cmd('1',b'KEY> ');assert b'AUTH 2' in out
            clients[1].send(b'g');assert b'SAVED' in clients[1].until(b'KEY> ')
            assert query(game_socket,{})['players']==[[1,1],[2,2]]
            # Stopping the parent supervisor revokes live workers/leases and reaps
            # their detached children before the durable slots become reusable.
            clients[1].send(b'f');clients[1].until(b'KEY> ')
            services[1].terminate();services[1].wait(timeout=10)
            clients[1].until(b'0:main] > ')
            assert not processes(SLOT1) and not processes(SLOT2)
            assert query(game_socket,{})['active']==0
            assert all(file.stat().st_size==0 for file in state.glob('slot-*'))
            from door_isolation import slot_acquire,Rejected
            dirty=state/('slot-'+str(SLOT1));dirty.write_bytes(b'BUSY\n')
            try:slot_acquire({'slots':[slots[0]],'state_dir':str(state)},1,0)
            except Rejected:pass
            else:raise AssertionError('uncertain crashed slot was reused')
            assert dirty.read_bytes()==b'BUSY\n'
            # No fallback to a same-UID Door when the isolation service is down.
            out=clients[1].cmd('1',b'0:main] > ');assert b'AUTH ' not in out
        finally:
            for c in clients:
                with contextlib.suppress(OSError):c.close()
            if bbs:bbs.close()
            if game:game.terminate();game.join(5)
            for process in services:
                if process.poll() is None:process.terminate()
                process.wait(timeout=5)
                errors=process.stderr.read(2048)
                if errors:print('Service diagnostics:',errors.decode('utf8','replace'),flush=True)
            for uid in (SLOT1,SLOT2):
                for _ in range(50):
                    if not processes(uid,terminate=sys.platform.startswith('linux')):break
                    time.sleep(.03)
                assert not processes(uid),'fixture process cleanup failed'
    print('PASS isolated Door: two authenticated members, UID separation, dropfile, one-use API, disconnect/descendant cleanup, lease revocation, no legacy fallback, NAWS, per-mutation ACL, replay rejection, persistent game restart, supervisor stop')

if __name__=='__main__':run(str(Path(sys.argv[1]).resolve()))
