"""Three real BBS runtimes: ring/star, reply IDs, edits, withdrawals, replay."""
import asyncio,hashlib,sqlite3,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'));sys.path.insert(0,str(ROOT))
from link_store import Store,Ed25519PrivateKey,identity,public_bytes
from link_bbs import Core,export_board,apply_pending
from webui.bridge import Bridge,response_data
from test_board import Server

async def exercise(binary,p):
 servers=[];cores=[];users=[];stores=[];dirs=[];board=hashlib.sha256(b'shared').hexdigest()
 keys=[Ed25519PrivateKey.generate() for _ in range(3)];ids=[identity(k.public_key()) for k in keys]
 origins={n:{'public':public_bytes(k.public_key()),'boards':{board}} for n,k in zip(ids,keys)}
 try:
  for i in range(3):
   d=p/str(i);d.mkdir();dirs.append(d)
   seed=Server(binary,d/'notes',members=d/'members',telnet=True)
   b=await Bridge('127.0.0.1',seed.port).open()
   await b.command('REGISTER link');await b.command('link-test-password');await b.close();seed.close()
   (d/'accounts').write_text('1 1\n');(d/'secret').write_text('link-test-password\n');(d/'secret').chmod(0o600)
   server=Server(binary,d/'notes',members=d/'members',telnet=True,options=['--link-accounts',str(d/'accounts')]);servers.append(server)
   core=await Core({'port':server.port,'login':'link','password_file':str(d/'secret')}).open();cores.append(core)
   user=await Bridge('127.0.0.1',server.port).open();await user.command('REGISTER alice');await user.command('user-test-password');users.append(user)
   assert 'ERR link denied' in (await user.command('LINKSNAP 1'))['text']
   stores.append(Store(d/'link.sqlite',keys[i],origins,{n:v for n,v in origins.items() if n!=ids[i]}))
  async def post(user,title,body,parent=0):
   await user.command(f'REPLY {parent} {title}' if parent else 'POST '+title)
   async with user.lock:
    for line in body.split('\n'):await user.literal(line)
   r=await user.command('.');assert 'SAVED ' in r['text'],r
  async def sync(edges):
   for _ in range(3):
    for store,core in zip(stores,cores):await export_board(store,core,board,1)
    for source,target in edges:
     a,b=stores[source],stores[target];events,cursor=a.batch(b.node,b.cursor(a.node))
     for event in events:b.accept(event,a.node)
     b.acknowledge(a.node,cursor)
    for store,core in zip(stores,cores):await apply_pending(store,core,{board:1})
  ring=[(0,1),(1,2),(2,0)];star=[(0,1),(1,0),(0,2),(2,0)]
  await post(users[0],'first','shared body');await sync(ring)
  for user in users:
   r=await user.command('READ 1');assert response_data(r['text'])['article']['body']=='shared body\r\n',r
  await post(users[1],'reply','reply body',1);await sync(star)
  for user in users:
   r=await user.command('READ 2');assert response_data(r['text'])['article']['parent']==1,r
  r=await users[0].command('RETITLE 1 renamed');assert 'RETITLED' in r['text'],r
  await sync(ring)
  for user in users:assert response_data((await user.command('READ 1'))['text'])['article']['title']=='renamed'
  await users[0].command('WITHDRAW 1 CONFIRM');await sync(star)
  for user in users:assert 'WITHDRAWN 1' in (await user.command('READ 1'))['text']
  await post(users[0],'maximum','\n'.join(['x'*4094]*64));await sync(ring)
  for user in users:assert len(response_data((await user.command('READ 3'))['text'])['article']['body'].encode())==262144
  # Replaying every signed event never appends another local article.
  for i in (1,2):
   before=(dirs[i]/'notes').stat().st_size
   with stores[i].db:stores[i].db.execute('UPDATE events SET applied=0')
   await apply_pending(stores[i],cores[i],{board:1})
   assert (dirs[i]/'notes').stat().st_size==before
  for i in range(3):
   await users[i].close();await cores[i].close();servers[i].close()
   # Simulate interruption after applying a change but before its receipt.
   with sqlite3.connect(str(dirs[i]/'notes')+'.features.sqlite') as db:
    db.execute('UPDATE link_articles SET done=0')
   before=(dirs[i]/'notes').stat().st_size
   servers[i]=Server(binary,dirs[i]/'notes',members=dirs[i]/'members',telnet=True,options=['--link-accounts',str(dirs[i]/'accounts')])
   assert (dirs[i]/'notes').stat().st_size==before
   cores[i]=await Core({'port':servers[i].port,'login':'link','password_file':str(dirs[i]/'secret')}).open()
   await cores[i].select(1);item=await cores[i].snapshot(3,True);assert item['id']==3 and len(item['body'].encode())==262144
  # An intent without a committed article is removed, never mistaken for a
  # later local article that happens to receive the reserved next number.
  await cores[1].close();servers[1].close()
  with sqlite3.connect(str(dirs[1]/'notes')+'.features.sqlite') as db:
   db.execute("INSERT INTO link_articles SELECT board,origin,?,4,1,'P',author,alias,created,0,'uncommitted',body_hash,0,actor FROM link_articles WHERE local_id=3",('f'*64,))
  servers[1]=Server(binary,dirs[1]/'notes',members=dirs[1]/'members',telnet=True,options=['--link-accounts',str(dirs[1]/'accounts')])
  with sqlite3.connect(str(dirs[1]/'notes')+'.features.sqlite') as db:assert db.execute('SELECT count(*) FROM link_articles WHERE local_id=4').fetchone()==(0,)
  servers[1].close()
  migrated=p/'migrated'
  subprocess.run([sys.executable,str(ROOT/'scripts/maintenance.py'),'db-migrate','--structured','--root',str(dirs[1]),'--board','notes','--members','members',str(migrated)],check=True,capture_output=True)
  restored=Server(binary,migrated/'notes',members=migrated/'members',telnet=True,options=['--database-structured',str(migrated/'liner.sqlite'),'--link-accounts',str(dirs[1]/'accounts')])
  probe=await Core({'port':restored.port,'login':'link','password_file':str(dirs[1]/'secret')}).open()
  try:
   await probe.select(1);item=await probe.snapshot(3,True)
   assert item['origin']==ids[0] and item['revision']==1 and len(item['body'])==262144
  finally:await probe.close();restored.close()
  print('PASS three real BBS links: ring/star, origin author, reply mapping, retitle, withdraw, 256KiB payload, replay, restart, interrupted intent recovery, unauthorized-account refusal')
 finally:
  for user in users:await user.close()
  for core in cores:await core.close()
  for server in servers:
   if server.proc.poll() is None:server.close()
  for store in stores:store.close()

if __name__=='__main__':
 with tempfile.TemporaryDirectory(prefix='link73-') as tmp:asyncio.run(exercise(str(Path(sys.argv[1]).resolve()),Path(tmp)))
