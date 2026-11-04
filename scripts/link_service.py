#!/usr/bin/env python3
"""Liner notes shared-board link daemon (opt-in, separate from the BBS)."""
import argparse,asyncio,fcntl,json,logging,os,secrets,signal,sqlite3,stat,tomllib
from pathlib import Path
from cryptography.hazmat.primitives.serialization import load_pem_private_key
from cryptography.exceptions import InvalidSignature
from link_store import Store,hexid,identity
from link_bbs import Core,BridgeError,export_board,apply_pending
import link_transport as transport

def private_file(path):
    path=Path(path)
    if path.is_symlink():raise ValueError('Secret file must not be a symlink')
    st=path.stat()
    if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1 or st.st_mode&0o077:raise ValueError('Secret files must have mode 0600 and one link')
    return path

def configuration(filename):
    with open(filename,'rb') as f:cfg=tomllib.load(f)
    node=cfg['node'];bbs=cfg['bbs'];directory=Path(node['identity']).resolve()
    key=load_pem_private_key(private_file(directory/'identity.key').read_bytes(),None)
    node_id=identity(key.public_key())
    if transport.certificate_node(directory/'identity.crt')[0]!=node_id:raise ValueError('Identity certificate/key mismatch')
    private_file(bbs['password_file'])
    boards={}
    for b in cfg['boards']:
        if not hexid(b['id']) or type(b['local']) is not int or not 1<=b['local']<=1024 or b['id'] in boards or b['local'] in boards.values():raise ValueError('Invalid or duplicate shared board')
        boards[b['id']]=b['local']
    if not boards:raise ValueError('No shared boards configured')
    origins={node_id:{'public':transport.certificate_node(directory/'identity.crt')[1],'boards':set(boards)}}
    peers={};endpoints={};certificates=[]
    for section in ('origins','peers'):
        target=origins if section=='origins' else peers
        for entry in cfg.get(section,[]):
            cert=Path(entry['certificate']).resolve();ident,public=transport.certificate_node(cert);allowed=set(entry['boards'])
            if ident in target or ident==node_id or not allowed or not allowed<=set(boards):raise ValueError('Invalid peer/origin board assignment')
            target[ident]={'public':public,'boards':allowed}
            if section=='peers':
                port=entry.get('port',9044)
                if type(port) is not int or not 1<=port<=65535:raise ValueError('Invalid peer port')
                endpoints[ident]=(entry['host'],port);certificates.append(cert)
    if not peers:raise ValueError('No registered peers')
    port=node.get('port',9044);interval=node.get('interval',5);maximum=node.get('max_events',1000000);capacity=node.get('max_bytes',1073741824)
    if type(port) is not int or not 1<=port<=65535 or type(interval) is not int or not 1<=interval<=3600 or type(maximum) is not int or maximum<1 or type(capacity) is not int or capacity<1048576:raise ValueError('Invalid link limits')
    if not isinstance(bbs.get('login'),str) or not bbs['login'] or any(c.isspace() for c in bbs['login']):raise ValueError('Invalid BBS login')
    return cfg,key,boards,origins,peers,endpoints,certificates


def inspect_store(filename,reset_peer=None):
    cfg,key,boards,origins,peers,_,_=configuration(filename)
    path=Path(cfg['node']['database']).absolute()
    if not path.is_file() or path.is_symlink():raise ValueError('Link database is missing or unsafe')
    lock=None
    try:
        if reset_peer is not None:
            if reset_peer not in peers:raise ValueError('Unknown peer; use its full public-key fingerprint')
            lock=os.open(str(path)+'.lock',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        with sqlite3.connect(path.as_uri()+'?mode='+('rw' if reset_peer else 'ro'),uri=True) as db:
            if reset_peer:
                db.execute('DELETE FROM cursors WHERE peer=?',(reset_peer,));db.commit()
            return dict(node=identity(key.public_key()),events=db.execute('SELECT count(*) FROM events').fetchone()[0],
                        pending=db.execute('SELECT count(*) FROM events WHERE applied=0').fetchone()[0],
                        deferred=db.execute('SELECT count(*) FROM retry').fetchone()[0],
                        cursors=dict(db.execute('SELECT peer,sequence FROM cursors')),
                        retry_reasons=list(db.execute('SELECT seq,reason FROM retry ORDER BY seq LIMIT 20')))
    finally:
        if lock is not None:os.close(lock)

async def run(filename):
    cfg,key,boards,origins,peers,endpoints,certificates=configuration(filename);node=cfg['node']
    database=Path(node['database']).absolute();database.parent.mkdir(mode=0o700,parents=True,exist_ok=True)
    lock=os.open(str(database)+'.lock',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
    fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    store=Store(database,key,origins,peers,max_events=node.get('max_events',1000000),max_bytes=node.get('max_bytes',1073741824));core=None;server=None;tasks=set()
    server_ctx=transport.context(node['identity'],certificates,True);client_ctx=transport.context(node['identity'],certificates)
    stop=asyncio.Event();loop=asyncio.get_running_loop()
    for sig in (signal.SIGTERM,signal.SIGINT):loop.add_signal_handler(sig,stop.set)
    async def client(reader,writer):
        task=asyncio.current_task()
        if len(tasks)>=8:writer.close();await writer.wait_closed();return
        tasks.add(task)
        try:await transport.serve(store,reader,writer)
        except (OSError,ValueError,asyncio.TimeoutError,asyncio.IncompleteReadError):logging.warning('Rejected or interrupted link exchange')
        finally:tasks.discard(task)
    try:
        server=await asyncio.start_server(client,node.get('listen','127.0.0.1'),node.get('port',9044),ssl=server_ctx,ssl_handshake_timeout=10)
        logging.info('Link ready; node %s',store.node);pending_cursor=0
        while not stop.is_set():
            for peer,(host,port) in endpoints.items():
                try:
                    for _ in range(16):
                        old=store.cursor(peer);await transport.pull(store,peer,host,port,client_ctx)
                        if store.cursor(peer)==old:break
                except (OSError,ValueError,InvalidSignature,asyncio.TimeoutError,asyncio.IncompleteReadError):logging.warning('Peer %s exchange incomplete; retained for retry',peer)
            try:
                if core is None:core=await Core(cfg['bbs']).open()
                for board,local in boards.items():await export_board(store,core,board,local)
                _,pending_cursor=await apply_pending(store,core,boards,pending_cursor)
            except (OSError,ValueError,RuntimeError,BridgeError):
                logging.warning('Local BBS exchange incomplete; events retained for retry')
                if core:await core.close();core=None
            try:await asyncio.wait_for(stop.wait(),node.get('interval',5))
            except asyncio.TimeoutError:pass
    finally:
        if server:server.close();await server.wait_closed()
        for task in list(tasks):task.cancel()
        if tasks:await asyncio.gather(*tasks,return_exceptions=True)
        if core:await core.close()
        store.close();os.close(lock)

def main():
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    init=sub.add_parser('init');init.add_argument('directory');init.add_argument('--name',required=True)
    inspect=sub.add_parser('inspect');inspect.add_argument('certificate')
    sub.add_parser('new-board')
    serve=sub.add_parser('serve');serve.add_argument('--config',required=True)
    check=sub.add_parser('check');check.add_argument('--config',required=True)
    status=sub.add_parser('status');status.add_argument('--config',required=True)
    reset=sub.add_parser('reset-peer');reset.add_argument('--config',required=True);reset.add_argument('peer')
    a=p.parse_args();logging.basicConfig(level=logging.INFO,format='%(asctime)s %(message)s')
    try:
        if a.command=='init':print(transport.generate(a.directory,a.name))
        elif a.command=='inspect':print(transport.certificate_node(a.certificate)[0])
        elif a.command=='new-board':print(secrets.token_hex(32))
        elif a.command=='check':configuration(a.config);print('Link configuration OK')
        elif a.command in ('status','reset-peer'):print(json.dumps(inspect_store(a.config,getattr(a,'peer',None)),ensure_ascii=False,indent=2))
        else:asyncio.run(run(a.config))
    except (OSError,ValueError,KeyError,TypeError,sqlite3.Error) as error:p.exit(1,str(error)+'\n')

if __name__=='__main__':main()
