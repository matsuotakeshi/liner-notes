"""Offline, bounded-memory link checkpoints and portable signed snapshots."""
from contextlib import contextmanager
from posix_locks import file_lock
import json,base64,hashlib
import os
from pathlib import Path
import sqlite3
import tempfile
from link_store import Store, canonical,Ed25519PublicKey,hexid


@contextmanager
def exclusive(database):
    with file_lock(str(database)+'.lock'):
        yield


def snapshot(store,destination,peer=None):
    """No private key, credentials, local IDs or peer cursors in the archive."""
    destination=Path(destination)
    if peer is not None and peer not in store.peers:raise ValueError('Unknown snapshot recipient')
    fd=os.open(destination,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600)
    count=0;digest=hashlib.sha256()
    try:
        with os.fdopen(fd,'wb') as stream:
            stream.write(canonical({'format':'liner-notes-link-snapshot','version':1})+b'\n')
            for board,origin,raw in store.db.execute('SELECT board,origin,envelope FROM events ORDER BY seq'):
                if board not in store.origins.get(origin,{}).get('boards',set()):continue
                if peer is not None and board not in store.peers[peer]['boards']:continue
                line=raw.encode('utf8')+b'\n';stream.write(line);digest.update(line);count+=1
            end=dict(format='liner-notes-link-snapshot-end',count=count,sha256=digest.hexdigest(),node=store.node)
            stream.write(canonical(dict(end=end,signature=base64.b64encode(store.key.sign(canonical(end))).decode()))+b'\n')
            stream.flush();os.fsync(stream.fileno())
    except BaseException:
        destination.unlink(missing_ok=True)
        raise
    return count


def bootstrap(source,destination,key,origins,peers,**limits):
    """Build a new DB; signatures/trust/size limits checked before publication."""
    destination=Path(destination).absolute()
    if destination.exists() or destination.is_symlink():raise ValueError('Bootstrap output must not exist')
    with tempfile.TemporaryDirectory(prefix='.link-bootstrap-',dir=destination.parent) as directory:
        private=Path(directory)/'link.sqlite';store=Store(private,key,origins,peers,**limits);count=0;lines=0;digest=hashlib.sha256();complete=False
        try:
            with open(source,'rb') as stream:
                if stream.readline(1024)!=canonical({'format':'liner-notes-link-snapshot','version':1})+b'\n':raise ValueError('Unknown snapshot format')
                while True:
                    raw=stream.readline(2*1024*1024)
                    if not raw:break
                    if not raw.endswith(b'\n'):raise ValueError('Oversized or incomplete snapshot record')
                    from link_transport import unique
                    envelope=json.loads(raw,object_pairs_hook=unique)
                    if not isinstance(envelope,dict):raise ValueError('Invalid snapshot record')
                    if isinstance(envelope,dict) and set(envelope)=={'end','signature'}:
                        end=envelope['end']
                        if not isinstance(end,dict) or set(end)!={'format','count','sha256','node'} or end['format']!='liner-notes-link-snapshot-end' or type(end['count']) is not int or end['count']!=lines or end['sha256']!=digest.hexdigest() or not hexid(end['node']):raise ValueError('Snapshot manifest mismatch')
                        sender=origins.get(end['node']) or peers.get(end['node'])
                        if not sender:raise ValueError('Unknown snapshot signer')
                        Ed25519PublicKey.from_public_bytes(sender['public']).verify(base64.b64decode(envelope['signature'],validate=True),canonical(end))
                        if stream.read(1):raise ValueError('Unexpected data after snapshot signature')
                        complete=True;break
                    if envelope.get('event',{}).get('origin')==store.node:raise ValueError('Bootstrap requires a new receiving node identity')
                    count+=store.accept(envelope)
                    digest.update(raw);lines+=1
            if not complete:raise ValueError('Snapshot is missing its signed completion manifest')
            if store.db.execute('PRAGMA integrity_check').fetchone()!=('ok',):raise ValueError('Snapshot DB integrity check failed')
            store.db.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        finally:store.close()
        # Exclusive hard link prevents a competing creator being overwritten.
        os.link(private,destination)
    return count


def backup(store,destination):
    path=Path(destination)
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600);os.close(fd)
    try:
        db=sqlite3.connect(path)
        try:store.db.backup(db)
        finally:db.close()
    except BaseException:
        path.unlink(missing_ok=True)
        raise


def report(store):
    db=store.db
    health={row[0]:dict(success=row[1],error=row[2],attempt=row[3]) for row in db.execute('SELECT peer,success,error,attempt FROM peer_health')}
    paused={row[0] for row in db.execute('SELECT peer FROM paused_peers')}
    return dict(node=store.node,protocol=2,
        events=db.execute('SELECT count(*) FROM events').fetchone()[0],
        pending=db.execute('SELECT count(*) FROM events WHERE applied=0').fetchone()[0],
        deferred=db.execute('SELECT count(*) FROM retry').fetchone()[0],
        bytes=db.execute('PRAGMA page_count').fetchone()[0]*db.execute('PRAGMA page_size').fetchone()[0],
        peers=[dict(id=peer,boards=sorted(policy['boards']),cursor=store.cursor(peer),paused=peer in paused,**health.get(peer,dict(success=0,error='',attempt=0))) for peer,policy in store.peers.items()],
        scans=[dict(board=b,next_id=n) for b,n in db.execute('SELECT board,next_id FROM scans')],
        retry_reasons=[dict(sequence=s,reason=r) for s,r in db.execute('SELECT seq,reason FROM retry ORDER BY seq LIMIT 20')])
