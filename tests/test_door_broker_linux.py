"""Root-only test in a hosted Linux container or an admitted disposable FreeBSD jail."""
from pathlib import Path
import contextlib,fcntl,json,multiprocessing,os,re,shutil,signal,socket,sqlite3,stat,subprocess,sys,tempfile,time
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

def wait_for(predicate,message,seconds=8):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        if predicate():return
        time.sleep(.03)
    raise AssertionError(message)

def slot_released(state,uid,processes):
    if processes(uid):return False
    try:
        with (state/('slot-'+str(uid))).open('rb') as lock:
            try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            except BlockingIOError:return False
            return os.fstat(lock.fileno()).st_size==0
    except FileNotFoundError:return True

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
    from door_processes import Scope
    for uid in (BBS,BROKER,GAME,SLOT1,SLOT2):assert not processes(uid),'fixture UID already active'
    # Reap only this test's descendants if a deliberately killed worker leaves
    # orphans. FreeBSD signalling stays within the kernel-owned reaper subtree.
    fixture_scope=Scope(SLOT1)
    with tempfile.TemporaryDirectory(prefix='ln-broker-',dir='/var/run' if sys.platform.startswith('freebsd') else '/run') as tmp:
        p=Path(tmp);p.chmod(0o755);scripts=p/'scripts';scripts.mkdir();scripts.chmod(0o755)
        # The jail helper deliberately retains umask 077. Publish only fixture
        # code and non-secret policy explicitly; do not relax the process umask
        # or recursively expose BBS data, reservations, tickets or game saves.
        for name in ('door_broker.py','door_broker_core.py','door_isolation.py','door_auth_client.py','door_processes.py'):
            shutil.copy2(ROOT/'scripts'/name,scripts/name);(scripts/name).chmod(0o644)
        runtime=p/'runtime';shutil.copy2(binary,runtime);runtime.chmod(0o755)
        data=p/'data';data.mkdir(mode=0o700);os.chown(data,BBS,BBS)
        (data/'private').write_text('private BBS fixture');os.chown(data/'private',BBS,BBS);(data/'private').chmod(0o600)
        broker_dir=p/'broker';broker_dir.mkdir(mode=0o755);broker_dir.chmod(0o755);os.chown(broker_dir,BROKER,BROKER)
        state=p/'state';state.mkdir(mode=0o711);state.chmod(0o711)
        game_dir=p/'server';game_dir.mkdir(mode=0o711);game_dir.chmod(0o711);os.chown(game_dir,GAME,GAME)
        endpoint=broker_dir/'broker.sock';game_socket=game_dir/'game.sock'
        broker_conf=p/'broker.json';broker_conf.write_text(json.dumps(dict(bbs='fixture',bbs_uid=BBS,broker_uid=BROKER,socket=str(endpoint),socket_gid=GAME,door_uids=[SLOT1,SLOT2],consumers={str(GAME):[1]})))
        broker_conf.chmod(0o644)
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
print('AUTH '+str(claims['member'])+' UID '+str(os.getuid())+' WORKER '+str(os.getppid())+'\r\nKEY> ',end='',flush=True)
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
        doors.chmod(0o644)
        commands=[([sys.executable,str(scripts/'door_broker.py'),'--config',str(broker_conf)],identity(BROKER,[GAME])),([sys.executable,str(scripts/'door_isolation.py'),'--config',str(launch_conf)],None)]
        services=[];clients=[];bbs=None;game=None;crash_marker=None
        def released():return all(slot_released(state,uid,processes) for uid in (SLOT1,SLOT2))
        def start_launcher():
            process=subprocess.Popen(commands[1][0],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            services.append(process);ready(isolation,process)
            return process
        def lobby():
            wait_for(released,'previous Door cleanup not complete')
            out=clients[1].cmd('1',b'KEY> ')
            assert b'AUTH 2 UID 64504' in out,'wrong member or reused occupied slot'
            match=re.search(rb'WORKER (\d+)',out);assert match,'missing fixture worker identity'
            return int(match[1])
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
            # An authenticated BBS UID must not extend a request deadline by
            # trickling bytes. Existing two Doors/leases remain live throughout.
            slow_request=r'''
import select,socket,sys,time
with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as s:
    s.settimeout(2);s.connect(sys.argv[1]);end=time.monotonic()+6
    result=b''
    while time.monotonic()<end:
        try:s.sendall(b' ')
        except BrokenPipeError:pass
        if select.select([s],[],[],.25)[0]:
            result+=s.recv(128)
            if b'ERR\n' in result:break
    assert b'ERR\n' in result,'partial request kept admission alive'
'''
            subprocess.run([sys.executable,'-c',slow_request,str(isolation)],preexec_fn=identity(BBS),check=True,timeout=10)
            assert services[1].poll() is None,'partial request stopped supervisor'
            assert query(game_socket,{})['active']==2,'partial request revoked existing games'
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
            wait_for(released,'normal Door exit did not release reservations')
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
            # The hand-written dirty reservation above has never owned a process.
            # Restore only that synthetic value before exercising real crashes.
            with dirty.open('r+b') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB);lock.truncate(0)

            # A short policy deadline must revoke a live lease and stop detached
            # descendants even when the BBS listing advertises a longer duration.
            for slot in slots:slot['seconds']=3
            policy=json.loads(launch_conf.read_text());policy['slots']=slots
            launch_conf.write_text(json.dumps(policy))
            launcher=start_launcher();lobby()
            clients[1].send(b'f');clients[1].until(b'KEY> ')
            clients[1].until(b'0:main] > ')
            wait_for(released,'policy timeout left reserved/live Door')
            assert query(game_socket,{})['active']==0
            launcher.terminate();launcher.wait(timeout=10)
            for slot in slots:slot['seconds']=30
            policy['slots']=slots;launch_conf.write_text(json.dumps(policy))

            # SIGKILL bypasses the parent's finally. Its inherited watch pipe
            # must still revoke worker authority and reclaim detached games.
            for cycle in range(2):
                launcher=start_launcher();inode=isolation.lstat().st_ino;lobby()
                clients[1].send(b'f');clients[1].until(b'KEY> ')
                launcher.kill();launcher.wait(timeout=5)
                clients[1].until(b'0:main] > ')
                wait_for(released,'killed supervisor left live/reserved Door')
                assert query(game_socket,{})['active']==0
                assert query(game_socket,{})['players']==[[1,1],[2,2]]
                # Explicit recovery of this fixture's orphan socket only, after
                # parent death and proven cleanup. Do not reset reservation files.
                st=isolation.lstat();assert stat.S_ISSOCK(st.st_mode) and st.st_ino==inode
                isolation.unlink()

            # Losing the cleanup worker is different: revoke its lease, retain
            # BUSY, and refuse reuse while a detached game may still exist.
            launcher=start_launcher();worker=lobby()
            clients[1].send(b'f');clients[1].until(b'KEY> ')
            parent=subprocess.check_output(['ps','-p',str(worker),'-o','ppid='],text=True).strip()
            assert parent==str(launcher.pid),'fixture worker parent changed'
            os.kill(worker,signal.SIGKILL)
            clients[1].until(b'0:main] > ')
            wait_for(lambda:query(game_socket,{})['active']==0,'dead worker lease survived')
            crash_marker=state/('slot-'+str(SLOT1))
            assert crash_marker.read_bytes()==b'BUSY\n','worker crash silently released reservation'
            assert processes(SLOT1),'detached fixture did not survive worker loss'
            with crash_marker.open('rb') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
            try:slot_acquire({'slots':[slots[0]],'state_dir':str(state)},1,0)
            except Rejected:pass
            else:raise AssertionError('real crashed worker slot reused')
            assert crash_marker.read_bytes()==b'BUSY\n'
        finally:
            cleanup_errors=[]
            for c in clients:
                with contextlib.suppress(OSError):c.close()
            if bbs:
                try:bbs.close()
                except Exception as error:cleanup_errors.append(('bbs',type(error).__name__))
            if game:
                game.terminate();game.join(5)
                if game.is_alive():
                    game.kill();game.join(5);cleanup_errors.append(('game','stop timeout'))
            for process in services:
                if process.poll() is None:process.terminate()
                try:process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill();process.wait(timeout=5);cleanup_errors.append(('service','stop timeout'))
            # Reclaim adopted fixture orphans before reading stderr pipes to EOF.
            fixture_scope.stop()
            for uid in (SLOT1,SLOT2):
                for _ in range(50):
                    if not processes(uid,terminate=sys.platform.startswith('linux')):break
                    time.sleep(.03)
                assert not processes(uid),'fixture process cleanup failed'
            if crash_marker:
                assert crash_marker.read_bytes()==b'BUSY\n','orphan cleanup erased crash evidence'
            for process in services:
                errors=process.stderr.read(2048)
                if errors:print('Service diagnostics:',errors.decode('utf8','replace'),flush=True)
            assert not cleanup_errors,cleanup_errors
    print('PASS isolated Door: identity/ACL, PTY, persistence, graceful stop, policy deadline, repeated supervisor SIGKILL, worker SIGKILL revocation and durable refusal')

if __name__=='__main__':run(str(Path(sys.argv[1]).resolve()))
