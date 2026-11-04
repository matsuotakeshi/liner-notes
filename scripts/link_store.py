"""Signed, durable shared-board event store. No member or mail data model."""
import base64,hashlib,json,os,re,sqlite3,stat,time
from pathlib import Path
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey,Ed25519PublicKey
from cryptography.hazmat.primitives import serialization

MAX_BODY=262144
FIELDS={'version','origin','board','article','parent','revision','kind','author','title','body','created'}

def canonical(value):return json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(',',':'),allow_nan=False).encode('utf8')
def public_bytes(key):return key.public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
def identity(key):return hashlib.sha256(public_bytes(key)).hexdigest()
def hexid(value):return isinstance(value,str) and re.fullmatch(r'[0-9a-f]{64}',value) is not None

def validate(event):
    if not isinstance(event,dict) or set(event)!=FIELDS or type(event['version']) is not int or event['version'] not in (1,2):raise ValueError('Unknown link event schema')
    if not all(hexid(event[k]) for k in ('origin','board','article')):raise ValueError('Invalid link identity')
    if event['parent'] is not None and (not isinstance(event['parent'],dict) or set(event['parent'])!={'origin','article'} or not all(hexid(v) for v in event['parent'].values())):raise ValueError('Invalid parent')
    if event['parent']=={'origin':event['origin'],'article':event['article']}:raise ValueError('Self parent')
    if type(event['revision']) is not int or not 1<=event['revision']<=2147483646:raise ValueError('Invalid revision')
    if type(event['created']) is not int or not 0<event['created']<=253402300799:raise ValueError('Invalid timestamp')
    kinds=('post','title','withdraw') if event['version']==1 else ('checkpoint','tombstone')
    if event['kind'] not in kinds:raise ValueError('Unknown event kind')
    for key,limit in (('author',32),('title',80),('body',MAX_BODY)):
        value=event[key]
        if not isinstance(value,str) or len(value.encode('utf8'))>limit or '\0' in value:raise ValueError('Invalid '+key)
        if any(ord(c)<32 and (key!='body' or c not in '\r\n\t') or ord(c)==127 for c in value):raise ValueError('Control character')
    if not event['author']:raise ValueError('Empty public author name')
    if event['kind']=='checkpoint':
        if not event['title'] or not event['body']:raise ValueError('Empty checkpoint')
    elif event['kind']=='tombstone':
        if event['title'] or event['body']:raise ValueError('Tombstone contains content')
    elif event['kind']=='post':
        if event['revision']!=1 or not event['title'] or not event['body']:raise ValueError('Invalid initial post')
    elif event['revision']==1 or event['body']:raise ValueError('Invalid update')
    if event['kind']=='title' and not event['title']:raise ValueError('Empty title')
    if event['kind']=='withdraw' and event['title']:raise ValueError('Withdrawal contains title')

class Store:
    def __init__(self,path,private_key,origins,peers,*,max_events=1000000,max_bytes=1073741824):
        """origins and peers map node ID -> {public: raw bytes, boards: set}.

        Origin trust is separate from forwarding-peer trust. A trusted relay
        cannot sign for another origin or disclose an unassigned board.
        """
        self.key=private_key;self.node=identity(private_key.public_key());self.origins=origins;self.peers=peers;self.maximum=max_events;self.max_bytes=max_bytes
        path=Path(path)
        if path.exists() and (path.is_symlink() or path.stat().st_nlink!=1):raise ValueError('Unsafe link DB')
        fd=os.open(path,os.O_RDWR|os.O_CREAT|getattr(os,'O_NOFOLLOW',0),0o600)
        try:
            st=os.fstat(fd)
            if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1:raise ValueError('Unsafe link DB')
        finally:os.close(fd)
        self.db=sqlite3.connect(path);self.db.execute('PRAGMA journal_mode=WAL');self.db.execute('PRAGMA synchronous=FULL')
        self.db.executescript('''
          CREATE TABLE IF NOT EXISTS events(seq INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE NOT NULL,board TEXT NOT NULL,origin TEXT NOT NULL,article TEXT NOT NULL,revision INTEGER NOT NULL,envelope TEXT NOT NULL,applied INTEGER NOT NULL DEFAULT 0,UNIQUE(board,origin,article,revision));
          CREATE INDEX IF NOT EXISTS event_apply ON events(applied,seq);
          CREATE TABLE IF NOT EXISTS cursors(peer TEXT PRIMARY KEY,sequence INTEGER NOT NULL);
          CREATE TABLE IF NOT EXISTS local_articles(board TEXT,local_id INTEGER,article TEXT,revision INTEGER,title TEXT,withdrawn INTEGER,PRIMARY KEY(board,local_id));
          CREATE TABLE IF NOT EXISTS retry(seq INTEGER PRIMARY KEY,until INTEGER NOT NULL,reason TEXT NOT NULL);
          CREATE TABLE IF NOT EXISTS scans(board TEXT PRIMARY KEY,next_id INTEGER NOT NULL);
          CREATE TABLE IF NOT EXISTS checkpoints(board TEXT,origin TEXT,article TEXT,revision INTEGER NOT NULL,id TEXT NOT NULL,PRIMARY KEY(board,origin,article));
          CREATE TABLE IF NOT EXISTS peer_health(peer TEXT PRIMARY KEY,success INTEGER,error TEXT,attempt INTEGER);
          CREATE TABLE IF NOT EXISTS paused_peers(peer TEXT PRIMARY KEY);
          CREATE TABLE IF NOT EXISTS article_heads(board TEXT,origin TEXT,article TEXT,revision INTEGER NOT NULL,kind TEXT NOT NULL,title TEXT NOT NULL,author TEXT NOT NULL,PRIMARY KEY(board,origin,article));
          CREATE INDEX IF NOT EXISTS event_board_seq ON events(board,seq);
          CREATE INDEX IF NOT EXISTS event_origin_article ON events(origin,board,article,revision);
          CREATE TABLE IF NOT EXISTS event_totals(id INTEGER PRIMARY KEY CHECK(id=1),total INTEGER NOT NULL);
          INSERT INTO event_totals SELECT 1,count(*) FROM events WHERE NOT EXISTS(SELECT 1 FROM event_totals) ON CONFLICT(id) DO NOTHING;
          CREATE TRIGGER IF NOT EXISTS event_count_insert AFTER INSERT ON events BEGIN UPDATE event_totals SET total=total+1 WHERE id=1; END;
          CREATE TRIGGER IF NOT EXISTS event_count_delete AFTER DELETE ON events BEGIN UPDATE event_totals SET total=total-1 WHERE id=1; END;
        ''')
        # Metadata-only lookup avoids parsing a full article body on every scan.
        # Incremental backfill also supports a 0.73 store without rewriting events.
        if self.db.execute('PRAGMA user_version').fetchone()[0]<1:
          with self.db:
            self.db.execute('''INSERT INTO article_heads
              SELECT e.board,e.origin,e.article,e.revision,json_extract(e.envelope,'$.event.kind'),json_extract(e.envelope,'$.event.title'),json_extract(e.envelope,'$.event.author')
              FROM events e WHERE NOT EXISTS(SELECT 1 FROM article_heads h WHERE h.board=e.board AND h.origin=e.origin AND h.article=e.article)
              AND e.revision=(SELECT max(p.revision) FROM events p WHERE p.board=e.board AND p.origin=e.origin AND p.article=e.article)
              ON CONFLICT(board,origin,article) DO NOTHING''')
            self.db.execute('PRAGMA user_version=1')
    def close(self):self.db.close()
    def sign(self,event):
        validate(event)
        if event['origin']!=self.node:raise ValueError('Cannot impersonate another origin')
        raw=canonical(event)
        return {'event':event,'signature':base64.b64encode(self.key.sign(raw)).decode('ascii')}
    def accept(self,envelope,peer=None):
        if not isinstance(envelope,dict) or set(envelope)!={'event','signature'}:raise ValueError('Invalid signed envelope')
        event=envelope['event'];validate(event)
        origin=self.origins.get(event['origin'])
        if not origin or event['board'] not in origin['boards']:raise ValueError('Origin is not allowed on this board')
        if identity(Ed25519PublicKey.from_public_bytes(origin['public']))!=event['origin']:raise ValueError('Origin public key mismatch')
        if peer is not None and (peer not in self.peers or event['board'] not in self.peers[peer]['boards']):raise ValueError('Peer board not shared')
        raw=canonical(event)
        if not isinstance(envelope['signature'],str) or len(envelope['signature'])!=88:raise ValueError('Invalid signature encoding')
        signature=base64.b64decode(envelope['signature'],validate=True)
        Ed25519PublicKey.from_public_bytes(origin['public']).verify(signature,raw)
        ident=hashlib.sha256(raw).hexdigest()
        floor=self.db.execute('SELECT revision,id FROM checkpoints WHERE board=? AND origin=? AND article=?',(event['board'],event['origin'],event['article'])).fetchone()
        if floor and event['revision']<=floor[0]:
            if event['revision']==floor[0] and ident!=floor[1]:raise ValueError('Conflicting checkpoint revision')
            return False
        existing=self.db.execute('SELECT id FROM events WHERE board=? AND origin=? AND article=? AND revision=?',(event['board'],event['origin'],event['article'],event['revision'])).fetchone()
        if existing:
            if existing[0]!=ident:raise ValueError('Conflicting signed revision')
            return False
        if self.db.execute('SELECT total FROM event_totals WHERE id=1').fetchone()[0]>=self.maximum:raise ValueError('Link event storage limit reached')
        used=self.db.execute('PRAGMA page_count').fetchone()[0]*self.db.execute('PRAGMA page_size').fetchone()[0]
        if used+len(canonical(envelope))+16384>self.max_bytes:raise ValueError('Link database size limit reached')
        with self.db:
            self.db.execute('INSERT INTO events(id,board,origin,article,revision,envelope) VALUES(?,?,?,?,?,?)',(ident,event['board'],event['origin'],event['article'],event['revision'],canonical(envelope).decode('utf8')))
            self.db.execute('INSERT INTO article_heads VALUES(?,?,?,?,?,?,?) ON CONFLICT(board,origin,article) DO UPDATE SET revision=excluded.revision,kind=excluded.kind,title=excluded.title,author=excluded.author WHERE excluded.revision>article_heads.revision',(event['board'],event['origin'],event['article'],event['revision'],event['kind'],event['title'],event['author']))
            if event['version']==2:
                self.db.execute('INSERT INTO checkpoints VALUES(?,?,?,?,?) ON CONFLICT(board,origin,article) DO UPDATE SET revision=excluded.revision,id=excluded.id',(event['board'],event['origin'],event['article'],event['revision'],ident))
        return True
    def batch(self,peer,after,limit=4):
        if peer not in self.peers or type(after) is not int or not 0<=after<=9223372036854775807:raise ValueError('Unknown peer or cursor')
        if not 1<=limit<=4:raise ValueError('Invalid batch size')
        row=self.db.execute("SELECT seq FROM sqlite_sequence WHERE name='events'").fetchone();latest=row[0] if row else 0
        if after>latest:raise ValueError('Peer cursor is ahead of this store; reconcile restored link DB')
        rows=self.db.execute('SELECT seq,board,origin,envelope FROM events WHERE seq>? ORDER BY seq LIMIT 256',(after,)).fetchall()
        events=[];cursor=after
        for seq,board,origin,envelope in rows:
            cursor=seq
            if board in self.peers[peer]['boards'] and board in self.origins.get(origin,{}).get('boards',set()):events.append(json.loads(envelope))
            if len(events)==limit:break
        return events,cursor
    def pending(self,limit=64,after=0):
        return [(seq,json.loads(raw)['event']) for seq,raw in self.db.execute('SELECT seq,envelope FROM events WHERE applied=0 AND seq>? AND seq NOT IN (SELECT seq FROM retry WHERE until>?) ORDER BY seq LIMIT ?',(after,int(time.time()),limit))]
    def defer(self,seq,reason):
        with self.db:self.db.execute('INSERT INTO retry VALUES(?,?,?) ON CONFLICT(seq) DO UPDATE SET until=excluded.until,reason=excluded.reason',(seq,int(time.time())+60,reason[:160]))
    def applied(self,seq):
        with self.db:
            self.db.execute('UPDATE events SET applied=1 WHERE seq=?',(seq,));self.db.execute('DELETE FROM retry WHERE seq=?',(seq,))
    def cursor(self,peer):
        row=self.db.execute('SELECT sequence FROM cursors WHERE peer=?',(peer,)).fetchone();return row[0] if row else 0
    def acknowledge(self,peer,cursor):
        if peer not in self.peers or type(cursor) is not int or not self.cursor(peer)<=cursor<=9223372036854775807:raise ValueError('Invalid acknowledgement')
        with self.db:self.db.execute('INSERT INTO cursors VALUES(?,?) ON CONFLICT(peer) DO UPDATE SET sequence=excluded.sequence',(peer,cursor))

    def health(self,peer,error=''):
        now=int(time.time())
        with self.db:
            self.db.execute("INSERT INTO peer_health VALUES(?,?,?,?) ON CONFLICT(peer) DO UPDATE SET success=CASE WHEN excluded.error='' THEN excluded.success ELSE peer_health.success END,error=excluded.error,attempt=excluded.attempt",(peer,now if not error else 0,error[:160],now))

    def make_checkpoints(self):
        """Only an origin can supersede its own immutable history. Offline operation."""
        made=0
        def articles():
            last=('','')
            while True:
                rows=self.db.execute('SELECT DISTINCT board,article FROM events WHERE origin=? AND (board,article)>(?,?) ORDER BY board,article LIMIT 128',(self.node,*last)).fetchall()
                if not rows:return
                yield from rows
                last=rows[-1]
        for board,article in articles():
            if board not in self.origins.get(self.node,{}).get('boards',set()):continue
            raw=self.db.execute('SELECT envelope FROM events WHERE board=? AND origin=? AND article=? ORDER BY revision DESC LIMIT 1',(board,self.node,article)).fetchone()[0]
            latest=json.loads(raw)['event']
            if latest['version']==2:continue
            withdrawn=latest['kind']=='withdraw'
            body=''
            if not withdrawn:
                for (raw,) in self.db.execute('SELECT envelope FROM events WHERE board=? AND origin=? AND article=? ORDER BY revision DESC',(board,self.node,article)):
                    prior=json.loads(raw)['event']
                    if prior['kind'] in ('post','checkpoint'):body=prior['body'];break
                if not body:raise ValueError('Missing checkpoint body')
            event=dict(latest,version=2,kind='tombstone' if withdrawn else 'checkpoint',revision=latest['revision']+1,body=body,title='' if withdrawn else latest['title'])
            made+=self.accept(self.sign(event))
            with self.db:self.db.execute('UPDATE events SET applied=1 WHERE id=?',(hashlib.sha256(canonical(event)).hexdigest(),))
        return made

    def compact(self):
        """Retain signed checkpoints; no purge until that checkpoint reached the local BBS."""
        self.db.execute('PRAGMA secure_delete=ON')
        with self.db:
            eligible='SELECT e.seq FROM events e JOIN checkpoints c USING(board,origin,article) WHERE e.revision<c.revision AND EXISTS(SELECT 1 FROM events p WHERE p.id=c.id AND p.applied=1)'
            self.db.execute('DELETE FROM retry WHERE seq IN ('+eligible+')')
            removed=self.db.execute('DELETE FROM events WHERE seq IN ('+eligible+')').rowcount
        self.db.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        self.db.execute('VACUUM')
        self.db.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        return removed
