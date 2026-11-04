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
    if not isinstance(event,dict) or set(event)!=FIELDS or type(event['version']) is not int or event['version']!=1:raise ValueError('Unknown link event schema')
    if not all(hexid(event[k]) for k in ('origin','board','article')):raise ValueError('Invalid link identity')
    if event['parent'] is not None and (not isinstance(event['parent'],dict) or set(event['parent'])!={'origin','article'} or not all(hexid(v) for v in event['parent'].values())):raise ValueError('Invalid parent')
    if event['parent']=={'origin':event['origin'],'article':event['article']}:raise ValueError('Self parent')
    if type(event['revision']) is not int or not 1<=event['revision']<=2147483646:raise ValueError('Invalid revision')
    if type(event['created']) is not int or not 0<event['created']<=253402300799:raise ValueError('Invalid timestamp')
    if event['kind'] not in ('post','title','withdraw'):raise ValueError('Unknown event kind')
    for key,limit in (('author',32),('title',80),('body',MAX_BODY)):
        value=event[key]
        if not isinstance(value,str) or len(value.encode('utf8'))>limit or '\0' in value:raise ValueError('Invalid '+key)
        if any(ord(c)<32 and (key!='body' or c not in '\r\n\t') or ord(c)==127 for c in value):raise ValueError('Control character')
    if not event['author']:raise ValueError('Empty public author name')
    if event['kind']=='post':
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
        ''')
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
        existing=self.db.execute('SELECT id FROM events WHERE board=? AND origin=? AND article=? AND revision=?',(event['board'],event['origin'],event['article'],event['revision'])).fetchone()
        if existing:
            if existing[0]!=ident:raise ValueError('Conflicting signed revision')
            return False
        if self.db.execute('SELECT count(*) FROM events').fetchone()[0]>=self.maximum:raise ValueError('Link event storage limit reached')
        used=self.db.execute('PRAGMA page_count').fetchone()[0]*self.db.execute('PRAGMA page_size').fetchone()[0]
        if used+len(canonical(envelope))+16384>self.max_bytes:raise ValueError('Link database size limit reached')
        with self.db:self.db.execute('INSERT INTO events(id,board,origin,article,revision,envelope) VALUES(?,?,?,?,?,?)',(ident,event['board'],event['origin'],event['article'],event['revision'],canonical(envelope).decode('utf8')))
        return True
    def batch(self,peer,after,limit=4):
        if peer not in self.peers or type(after) is not int or not 0<=after<=9223372036854775807:raise ValueError('Unknown peer or cursor')
        if not 1<=limit<=4:raise ValueError('Invalid batch size')
        latest=self.db.execute('SELECT coalesce(max(seq),0) FROM events').fetchone()[0]
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
