"""Three-node ring/star relay, signatures, replay/restart and board isolation."""
import asyncio,copy,hashlib,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from link_store import Store,Ed25519PrivateKey,identity,public_bytes
from cryptography.exceptions import InvalidSignature
from link_bbs import export_board

class Links(unittest.TestCase):
 def test_bounded_export_restart(self):
  class Core:
   title='original';bodies=0
   async def select(self,board):assert board==1
   async def snapshot(self,cursor,body=False):
    if cursor>130:return None
    if body:self.bodies+=1
    return dict(id=cursor,parent=0,created=1,state='LIVE',origin='-',article='-',revision=0,author='alice',title=self.title if cursor==1 else 'article',body='body\r\n' if body else '')
  with tempfile.TemporaryDirectory() as tmp:
   key=Ed25519PrivateKey.generate();node=identity(key.public_key());board='a'*64
   origins={node:{'public':public_bytes(key.public_key()),'boards':{board}}};path=Path(tmp)/'store';core=Core()
   store=Store(path,key,origins,{})
   try:
    self.assertEqual(asyncio.run(export_board(store,core,board,1)),128)
    store.close();store=Store(path,key,origins,{})
    self.assertEqual(asyncio.run(export_board(store,core,board,1)),2)
    self.assertEqual(core.bodies,130)
    core.title='edited after first pass'
    self.assertEqual(asyncio.run(export_board(store,core,board,1)),1)
    self.assertEqual(core.bodies,130)
    self.assertEqual(store.db.execute('SELECT count(*) FROM events').fetchone()[0],131)
   finally:store.close()

 def test_topology(self):
  with tempfile.TemporaryDirectory() as tmp:
   keys=[Ed25519PrivateKey.generate() for _ in range(3)];ids=[identity(k.public_key()) for k in keys];board=hashlib.sha256(b'shared-board').hexdigest()
   origins={n:{'public':public_bytes(k.public_key()),'boards':{board}} for n,k in zip(ids,keys)}
   stores=[Store(Path(tmp)/str(i),k,origins,{n:v for n,v in origins.items() if n!=ids[i]}) for i,k in enumerate(keys)]
   event=dict(version=1,origin=ids[0],board=board,article='1'*64,parent=None,revision=1,kind='post',author='alice',title='日本語',body='hello\r\n',created=1)
   first=stores[0].sign(event);self.assertTrue(stores[0].accept(first))
   for edges in ([(0,1),(1,2),(2,0)],[(0,1),(0,2),(1,0),(2,0)]):
    for _ in range(4):
     for source,target in edges:
      a,b=stores[source],stores[target];batch,cursor=a.batch(b.node,b.cursor(a.node))
      for envelope in batch:b.accept(envelope,a.node)
      b.acknowledge(a.node,cursor)
   self.assertEqual([s.db.execute('SELECT count(*) FROM events').fetchone()[0] for s in stores],[1,1,1])
   for kind,title in [('title','renamed'),('withdraw','')]:
    event=dict(event,kind=kind,title=title,body='',revision=event['revision']+1)
    envelope=stores[0].sign(event);stores[0].accept(envelope);stores[1].accept(envelope,ids[0])
   stores[1].close();stores[1]=Store(Path(tmp)/'1',keys[1],origins,{n:v for n,v in origins.items() if n!=ids[1]})
   batch,cursor=stores[1].batch(ids[2],stores[2].cursor(ids[1]))
   for envelope in batch:stores[2].accept(envelope,ids[1])
   self.assertEqual(len(stores[2].pending()),3)
   bad=copy.deepcopy(first);bad['event']['title']='forged'
   with self.assertRaises(InvalidSignature):stores[2].accept(bad,ids[1])
   conflicting=stores[0].sign(dict(first['event'],title='conflict'))
   with self.assertRaises(ValueError):stores[1].accept(conflicting,ids[0])
   private=stores[0].sign(dict(first['event'],board='2'*64))
   with self.assertRaises(ValueError):stores[1].accept(private,ids[0])
   with self.assertRaises(ValueError):stores[1].accept(first,'3'*64)
   for signature in (None,[],True,'x'*100):
    with self.assertRaises(ValueError):stores[1].accept(dict(first,signature=signature),ids[0])
   with self.assertRaises(ValueError):stores[0].batch(ids[1],10**100)
   with self.assertRaises(ValueError):stores[1].acknowledge(ids[0],10**100)
   stores[1].defer(1,'temporary failure');self.assertNotIn(1,[seq for seq,_ in stores[1].pending()])
   stores[1].applied(1);self.assertEqual(stores[1].db.execute('SELECT count(*) FROM retry').fetchone()[0],0)
   stores[2].origins={n:dict(v,boards=set() if n==ids[0] else v['boards']) for n,v in origins.items()}
   self.assertEqual(stores[2].batch(ids[1],0)[0],[])
   stores[0].maximum=3
   with self.assertRaises(ValueError):stores[0].accept(stores[0].sign(dict(first['event'],article='4'*64)))
   for s in stores:s.close()

if __name__=='__main__':unittest.main()
