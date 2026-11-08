"""Signed checkpoints catch up a real BBS, including a missing withdrawn parent."""
import asyncio,sys,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'scripts')]
from test_board import Server
from webui.bridge import Bridge,response_data
from link_store import Store,Ed25519PrivateKey,identity,public_bytes
from link_bbs import Core,apply_pending

async def run(binary,p):
 s=Server(binary,p/'notes',members=p/'members',telnet=True)
 b=await Bridge('127.0.0.1',s.port).open()
 await b.command('REGISTER link');await b.command('link-test-password');await b.close();s.close()
 (p/'accounts').write_text('1 1\n');(p/'secret').write_text('link-test-password\n')
 s=Server(binary,p/'notes',members=p/'members',telnet=True,options=['--link-accounts',str(p/'accounts')])
 core=await Core(dict(port=s.port,login='link',password_file=str(p/'secret'))).open()
 b=await Bridge('127.0.0.1',s.port).open()
 keys=[Ed25519PrivateKey.generate() for _ in range(2)];ids=[identity(k.public_key()) for k in keys];board='a'*64
 origins={n:dict(public=public_bytes(k.public_key()),boards={board}) for n,k in zip(ids,keys)}
 a=Store(p/'a',keys[0],origins,{ids[1]:origins[ids[1]]});d=Store(p/'b',keys[1],origins,{ids[0]:origins[ids[0]]})
 try:
  event=dict(version=1,origin=ids[0],board=board,article='1'*64,parent=None,revision=1,kind='post',author='alice',title='parent',body='old secret',created=1)
  a.accept(a.sign(event));a.accept(a.sign(dict(event,kind='withdraw',revision=2,body='',title='')))
  reply=dict(event,article='2'*64,parent=dict(origin=ids[0],article=event['article']),title='reply',body='surviving reply')
  a.accept(a.sign(reply));a.make_checkpoints();a.compact()
  async def sync():
   rows,cursor=a.batch(ids[1],d.cursor(ids[0]))
   for row in rows:d.accept(row,ids[0])
   d.acknowledge(ids[0],cursor)
   for _ in range(3):await apply_pending(d,core,{board:1})
  await sync()
  assert 'WITHDRAWN 1' in (await b.command('READ 1'))['text']
  assert 'HIDDEN 2 PARENT 1' in (await b.command('READ 2'))['text']
  await core.select(1);item=await core.snapshot(2,True)
  assert item['parent']==1 and item['state']=='HIDDEN' and not item['body'] and not item['title'],item
  assert b'old secret' not in (p/'notes').read_bytes()
  a.accept(a.sign(dict(reply,kind='title',revision=3,title='edited after bootstrap',body='')))
  await sync();a.make_checkpoints();await sync()
  assert 'HIDDEN 2 PARENT 1' in (await b.command('READ 2'))['text']
  await core.select(1);assert await core.lookup(ids[0],reply['article'])==(2,4)
  assert len(await core.page(1))==2
  assert not await core.page(3)
  before=(p/'notes').stat().st_size
  with d.db:d.db.execute('UPDATE events SET applied=0')
  await apply_pending(d,core,{board:1})
  assert (p/'notes').stat().st_size==before
  print('PASS real BBS checkpoint bootstrap, withdrawn parent, title after bootstrap, replay and paged metadata')
 finally:
  a.close();d.close();await b.close();await core.close();s.close()

if __name__=='__main__':
 with tempfile.TemporaryDirectory() as tmp:asyncio.run(run(str(Path(sys.argv[1]).resolve()),Path(tmp)))
