"""Publication retry must be idempotent, private, locked and bound to metadata."""
import os,pathlib,sys,tempfile,hashlib,json
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
import vault_zmodem as gateway
from test_zmodem_workflows import fails,Engine,spool_contents
from posix_locks import file_lock
from zmodem_connector import TransferFailure
class Ledger:
 name='line-v1';chunk_size=257;upload_category='group/tools'
 def __init__(self):self.protocol=self;self.records={};self.lost=True
 def request(self,*_):return b'ROLE member'
 def check_upload(self):pass
 def begin(self,size,digest,name,description):
  n=len(self.records)+1;self.records[n]=[size,digest,name,description,bytearray(),False];return n
 def status(self,n):
  size,digest,_,_,data,committed=self.records[n];return ('COMMITTED' if committed else 'ACTIVE'),len(data),size,digest
 def write(self,n,offset,data):
  assert len(self.records[n][4])==offset;self.records[n][4].extend(data);return len(self.records[n][4])
 def finish(self,n):
  record=self.records[n];assert hashlib.sha256(record[4]).hexdigest()==record[1];record[5]=True
  if self.lost and n==2:self.lost=False;raise OSError('Lost final receipt')
  return record[0],record[1]
 def info(self,n):return self.records[n][0],self.records[n][1]
class Batch:
 def receive_many(self,directory,*args):
  args[-1]();paths=[]
  for name in ('a.bin','b.bin'):
   path=directory/name;path.write_bytes(name.encode()*100);paths.append(path)
  return paths

def run():
 if os.name!='posix':print('SKIP: private spool locking needs POSIX');return
 with tempfile.TemporaryDirectory() as d:
  root=pathlib.Path(d);c=Ledger()
  def receive(**kw):return gateway.runtime_receive(c,Batch(),'host',2222,kw.pop('user','alice'),root,description=kw.pop('description','description'),batch=True,**kw)
  fails(receive,gateway.PublicationFailure);assert len(c.records)==2
  # A new upload must not archive uncertain publication or duplicate files.
  fails(receive,gateway.PublicationFailure);assert len(c.records)==2
  work=next(iter(spool_contents(root)));assert work.stat().st_mode&0o077==0
  assert 'password' not in (work/'transfer.json').read_text()
  fails(lambda:receive(resume=True,description='changed'));assert work.exists()
  fails(lambda:receive(resume=True,user='bob'));assert spool_contents(root)==[work]
  with file_lock(work/'lock',create=False):
   fails(lambda:receive(resume=True),OSError);assert work.exists()
  target=work/'data/a.bin';original=target.read_bytes();target.write_bytes(b'tampered');fails(lambda:receive(resume=True));target.write_bytes(original)
  receipt=receive(resume=True);assert receipt[0]==2 and len(c.records)==2 and all(r[5] for r in c.records.values());assert not spool_contents(root)
  class Partial(Engine):
   def receive(self,directory,*args):
    (directory/'part.bin').write_bytes(b'partial');raise TransferFailure('Wire lost')
  def single(engine,resume=False):return gateway.runtime_receive(c,engine,'host',2222,'alice',root,resume=resume,description=None if resume else 'saved')
  fails(lambda:single(Partial(b'')));work=next(iter(spool_contents(root)));assert (work/'data/part.bin').read_bytes()==b'partial'
  class Continued:
   def receive_resume(self,directory,fdin,fdout,authorize):
    authorize();path=directory/'part.bin';assert path.read_bytes()==b'partial';path.write_bytes(b'partial-completed');return path
  receipt=single(Continued(),True);assert receipt[0]==3 and len(c.records)==3 and not spool_contents(root)
  fails(lambda:single(Partial(b'')));work=next(iter(spool_contents(root)))
  with file_lock(work/'lock',create=False):
   fails(lambda:single(Engine(b'fresh')),OSError)
   assert not list(root.glob('retained-*'))
  receipt=single(Engine(b'fresh'));assert receipt[0]==4 and len(c.records)==4
  retained=list(root.glob('retained-*'));assert len(retained)==1
  assert (retained[0]/'data/part.bin').read_bytes()==b'partial'
  assert not list(root.glob('pending-*'))
  assert retained[0].stat().st_mode&0o077==0
  # Another fresh upload remains usable and leaves the old partial untouched.
  assert single(Engine(b'another'))[0]==5
  assert spool_contents(root)==retained
 print('PASS: batch publication lost-receipt retry without duplication; private account/metadata binding; concurrent lock; staged integrity; retained single partial and continuation')
if __name__=='__main__':run()
