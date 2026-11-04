"""Shared boards through a dedicated, explicitly allowed BBS account."""
import base64,hashlib,ipaddress,re,sys
from pathlib import Path
try:
    from bbs_transport import Bridge,BridgeError
except ModuleNotFoundError as error:
    if error.name!='bbs_transport':raise
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'webui'))
    from bbs_transport import Bridge,BridgeError
from link_store import canonical

def article_key(node,board,local):return hashlib.sha256(canonical([node,board,local])).hexdigest()
def encode(text):return base64.b64encode(text.encode('utf8')).decode('ascii') or '-'

class Core:
    def __init__(self,config):self.config=config;self.bridge=None
    async def open(self):
        c=self.config
        if not ipaddress.ip_address(c.get('host','127.0.0.1')).is_loopback:raise ValueError('Local BBS connector requires loopback')
        password=Path(c['password_file']).read_text(encoding='utf8').rstrip('\r\n')
        self.bridge=await Bridge(c.get('host','127.0.0.1'),int(c.get('port',2222)),telnet=True).open()
        try:
            await self.bridge.command('LOGIN '+c['login']);await self.bridge.command(password)
            if not self.bridge.state.get('user') or self.bridge.state.get('admin'):raise ValueError('Use a registered non-administrator link account')
        except BaseException:await self.close();raise
        return self
    async def close(self):
        if self.bridge:await self.bridge.close();self.bridge=None
    async def select(self,board):
        result=await self.bridge.command('BOARD '+str(board))
        if self.bridge.state['board']!=board or 'ERR ' in result['text'] or self.bridge.state.get('secret'):raise ValueError('Shared board is not available to link account')
    async def snapshot(self,cursor,body=False):
        result=await self.bridge.command(('LINKSNAP ' if body else 'LINKSCAN ')+str(cursor));text=result['text']
        if re.search(r'^LINK END\r?$',text,re.M):return None
        match=re.search(r'^LINK ITEM (\d+) (\d+) (\d+) (LIVE|WITHDRAWN|HIDDEN) (\S+) (\S+) (\d+)\r\nAUTHOR ([^\r\n]*)\r\nTITLE ([^\r\n]*)\r\nBODY (\d+)\r\n',text,re.M)
        if not match:raise ValueError('Shared-board snapshot failed')
        n=int(match[10]);body=text[match.end():].encode('utf8')[:n]
        if len(body)!=n:raise ValueError('Incomplete shared-board body')
        return dict(id=int(match[1]),parent=int(match[2]),created=int(match[3]),state=match[4],origin=match[5],article=match[6],revision=int(match[7]),author=match[8],title=match[9],body=body.decode('utf8'))
    async def lookup(self,origin,article):
        text=(await self.bridge.command(f'LINKLOOKUP {origin} {article}'))['text']
        match=re.search(r'^LINK LOOKUP (\d+) (\d+)\r?$',text,re.M)
        if not match:raise ValueError('Shared-board lookup failed')
        return tuple(map(int,match.groups()))
    async def apply(self,event,parent=0):
        kind={'post':'P','title':'T','withdraw':'D'}[event['kind']]
        command=f"LINKAPPLY {event['origin']} {event['article']} {event['revision']} {parent} {event['created']} {kind} {encode(event['author'])} {encode(event['title'])} {encode(event['body'])}"
        b=self.bridge
        if len(command)>4096:
            async with b.lock:
                await b._send(f'CONNECTOR LINKBLOCK {b.token} {len(command)}')
                lines=[f'CONNECTOR DATA {b.token} '+command[i:i+3000] for i in range(0,len(command),3000)]
                for i in range(0,len(lines),24):await b._send_lines(lines[i:i+24])
                result=await b.exchange()
        else:result=await b.command(command)
        match=re.search(r'^LINK APPLIED (\d+) (\d+)\r?$',result['text'],re.M)
        if not match:
            errors=[line for line in result['text'].splitlines() if line.startswith('ERR ') and len(line)<160]
            raise ValueError('Shared-board apply failed; event remains queued: '+('; '.join(errors) or 'no completion receipt'))
        return int(match[1])

async def export_board(store,core,board,local):
    await core.select(local);row=store.db.execute('SELECT next_id FROM scans WHERE board=?',(board,)).fetchone();cursor=row[0] if row else 1;count=0
    # Bound each pass, and persist progress only after its events are durable.
    # Revisiting old metadata discovers edits/withdrawals without rereading bodies.
    for _ in range(128):
        item=await core.snapshot(cursor)
        if item is None:cursor=1;break
        cursor=item['id']+1
        if item['origin']!='-' or item['state']=='HIDDEN':continue
        key=article_key(store.node,board,item['id'])
        row=store.db.execute('SELECT envelope FROM events WHERE origin=? AND board=? AND article=? ORDER BY revision DESC LIMIT 1',(store.node,board,key)).fetchone()
        import json
        previous=json.loads(row[0])['event'] if row else None
        if previous:
            with store.db:store.db.execute('INSERT INTO local_articles VALUES(?,?,?,?,?,?) ON CONFLICT(board,local_id) DO NOTHING',(board,item['id'],key,previous['revision'],previous['title'],previous['kind']=='withdraw'))
        if previous and previous['kind']=='withdraw':continue
        parent=None
        if item['parent']:
            p=await core.snapshot(item['parent'])
            if not p or p['id']!=item['parent']:raise ValueError('Shared reply parent is outside the configured board')
            parent={'origin':store.node if p['origin']=='-' else p['origin'],'article':article_key(store.node,board,p['id']) if p['origin']=='-' else p['article']}
        tombstone=not previous and item['state']=='WITHDRAWN'
        if not previous:
            kind='post';revision=1
            if tombstone:item=dict(item,title='Withdrawn',body='[Article withdrawn at origin]\r\n')
            else:item=await core.snapshot(item['id'],body=True)
        elif item['state']=='WITHDRAWN':kind='withdraw';revision=previous['revision']+1
        elif item['title']!=previous['title']:kind='title';revision=previous['revision']+1
        else:continue
        event=dict(version=1,origin=store.node,board=board,article=key,parent=parent,revision=revision,kind=kind,author=previous['author'] if previous else item['author'],created=item['created'],title='' if kind=='withdraw' else item['title'],body=item['body'] if kind=='post' else '')
        count+=store.accept(store.sign(event))
        if tombstone:
            count+=store.accept(store.sign(dict(event,kind='withdraw',revision=2,title='',body='')));revision=2
        with store.db:store.db.execute('INSERT INTO local_articles VALUES(?,?,?,?,?,?) ON CONFLICT(board,local_id) DO UPDATE SET revision=excluded.revision,title=excluded.title,withdrawn=excluded.withdrawn',(board,item['id'],key,revision,item['title'],kind=='withdraw' or tombstone))
    with store.db:store.db.execute('INSERT INTO scans VALUES(?,?) ON CONFLICT(board) DO UPDATE SET next_id=excluded.next_id',(board,cursor))
    return count

async def apply_pending(store,core,boards,after=0):
    rows=store.pending(64,after);cursor=0;count=0
    for seq,event in rows:
        cursor=seq
        if event['board'] not in boards or event['board'] not in store.origins.get(event['origin'],{}).get('boards',set()):continue
        if event['origin']==store.node:store.applied(seq);continue
        await core.select(boards[event['board']]);ident,revision=await core.lookup(event['origin'],event['article'])
        if revision>=event['revision']:store.applied(seq);continue
        if revision+1!=event['revision']:continue
        parent=0
        if event['kind']=='post' and event['parent']:
            ref=event['parent']
            if ref['origin']==store.node:
                row=store.db.execute('SELECT local_id FROM local_articles WHERE board=? AND article=?',(event['board'],ref['article'])).fetchone();parent=row[0] if row else 0
            else:parent,_=await core.lookup(ref['origin'],ref['article'])
            if not parent:continue
        try:await core.apply(event,parent)
        except ValueError:
            store.defer(seq,'BBS rejected this operation; check board access, local moderation and predecessor state')
            continue
        store.applied(seq);count+=1
    return count,cursor if len(rows)==64 else 0
