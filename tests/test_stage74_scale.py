"""10,050 signed articles: bounded snapshot/bootstrap, compact and replay."""
import hashlib,json,resource,sys,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
from link_store import Store,Ed25519PrivateKey,identity,public_bytes
from link_maintenance import snapshot,bootstrap

with tempfile.TemporaryDirectory(prefix='ln74-scale-') as tmp:
 p=Path(tmp);keys=[Ed25519PrivateKey.generate() for _ in range(2)];ids=[identity(k.public_key()) for k in keys];board='a'*64
 origins={n:dict(public=public_bytes(k.public_key()),boards={board}) for n,k in zip(ids,keys)}
 peers=lambda n:{k:v for k,v in origins.items() if k!=n}
 before=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss;t=time.monotonic()
 a=Store(p/'a',keys[0],origins,peers(ids[0]),max_bytes=1024**3)
 for n in range(10050):
  event=dict(version=1,origin=ids[0],board=board,article=f'{n:064x}',parent=None,revision=1,kind='post',author='scale',title='article '+str(n),body='x'*1024,created=1)
  a.accept(a.sign(event))
  if n%10==0:a.accept(a.sign(dict(event,revision=2,kind='withdraw',body='',title='')))
 assert a.make_checkpoints()==10050
 removed=a.compact();assert removed==11055
 assert a.db.execute('SELECT total FROM event_totals').fetchone()==(10050,)
 assert snapshot(a,p/'snapshot')==10050
 assert bootstrap(p/'snapshot',p/'b',keys[1],origins,peers(ids[1]))==10050
 a.close();a=Store(p/'a',keys[0],origins,peers(ids[0]));b=Store(p/'b',keys[1],origins,peers(ids[1]))
 count=0
 while True:
  rows,cursor=a.batch(ids[1],b.cursor(ids[0]))
  if not rows:break
  for row in rows:assert not b.accept(row,ids[0]);count+=1
  b.acknowledge(ids[0],cursor)
 assert count==10050
 result=dict(articles=10050,withdrawn=1005,removed_events=removed,snapshot_bytes=(p/'snapshot').stat().st_size,seconds=time.monotonic()-t,rss_increase_kib=resource.getrusage(resource.RUSAGE_SELF).ru_maxrss-before,replay_duplicates=0)
 print(json.dumps(result),flush=True)
 a.close();b.close()
