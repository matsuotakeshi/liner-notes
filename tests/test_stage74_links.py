"""Compacted signed history: offline peer catch-up, bootstrap, replay, physical purge."""
import json,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from link_store import Store,Ed25519PrivateKey,identity,public_bytes
from link_maintenance import snapshot,bootstrap,report

class Checkpoints(unittest.TestCase):
 def test_compaction_bootstrap(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp);keys=[Ed25519PrivateKey.generate() for _ in range(3)];ids=[identity(k.public_key()) for k in keys];board='a'*64
   origins={n:dict(public=public_bytes(k.public_key()),boards={board}) for n,k in zip(ids,keys)}
   peers=lambda n:{k:v for k,v in origins.items() if k!=n}
   a=Store(root/'a',keys[0],origins,peers(ids[0]));b=Store(root/'b',keys[1],origins,peers(ids[1]))
   secret='REMOVED-BODY-74-MUST-NOT-SURVIVE'
   event=dict(version=1,origin=ids[0],board=board,article='1'*64,parent=None,revision=1,kind='post',author='alice',title='old',body=secret,created=1)
   first=a.sign(event);a.accept(first)
   a.accept(a.sign(dict(event,kind='withdraw',revision=2,title='',body='')))
   reply=dict(event,article='2'*64,parent=dict(origin=ids[0],article='1'*64),title='reply',body='remaining')
   a.accept(a.sign(reply));self.assertEqual(a.make_checkpoints(),2)
   self.assertEqual(a.make_checkpoints(),0)
   self.assertEqual(a.compact(),3)
   self.assertNotIn(secret.encode(),(root/'a').read_bytes())
   envelopes,cursor=a.batch(ids[1],0)
   self.assertEqual(len(envelopes),2)
   for e in envelopes:b.accept(e,ids[0])
   b.acknowledge(ids[0],cursor)
   self.assertFalse(b.accept(first,ids[0]),'old post cannot resurrect withdrawn body')
   self.assertEqual(b.compact(),0,'unapplied checkpoint must not remove predecessors')
   self.assertEqual(snapshot(a,root/'snapshot'),2)
   self.assertEqual(bootstrap(root/'snapshot',root/'c',keys[2],origins,peers(ids[2])),2)
   c=Store(root/'c',keys[2],origins,peers(ids[2]));self.assertEqual(len(c.pending()),2)
   self.assertNotIn(secret,(root/'snapshot').read_text())
   self.assertEqual(report(c)['pending'],2)
   with self.assertRaises(ValueError):bootstrap(root/'snapshot',root/'c',keys[2],origins,peers(ids[2]))
   damaged=(root/'snapshot').read_text().replace('remaining','tampered')
   (root/'bad').write_text(damaged)
   with self.assertRaises(Exception):bootstrap(root/'bad',root/'failed',keys[2],origins,peers(ids[2]))
   self.assertFalse((root/'failed').exists())
   (root/'truncated').write_bytes(b'\n'.join((root/'snapshot').read_bytes().split(b'\n')[:-2])+b'\n')
   with self.assertRaises(ValueError):bootstrap(root/'truncated',root/'incomplete',keys[2],origins,peers(ids[2]))
   self.assertFalse((root/'incomplete').exists(),'newline-aligned truncation must not publish a partial snapshot')
   for store in (a,b,c):store.close()

if __name__=='__main__':unittest.main()
