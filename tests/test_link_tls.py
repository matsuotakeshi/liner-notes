"""Real mutual TLS: pinned keys, pull/replay, unregistered and wrong server."""
import asyncio,hashlib,struct,sys,tempfile,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import link_transport as tls
from link_store import Store
from cryptography.hazmat.primitives.serialization import load_pem_private_key

class Transport(unittest.IsolatedAsyncioTestCase):
 async def test_malformed_frames(self):
  for raw in (b'{"version":1,"version":1}',b'['*2000+b']'*2000,b'NaN'):
   reader=asyncio.StreamReader();reader.feed_data(struct.pack('!I',len(raw))+raw);reader.feed_eof()
   with self.assertRaises(ValueError):await tls.receive(reader)
  reader=asyncio.StreamReader();reader.feed_data(struct.pack('!I',tls.FRAME_MAX+1))
  with self.assertRaises(ValueError):await tls.receive(reader)
 async def test_exchange(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);ids=[tls.generate(p/str(i),'BBS '+str(i)) for i in range(3)]
   board=hashlib.sha256(b'shared').hexdigest()
   origins={n:{'public':tls.certificate_node(p/str(i)/'identity.crt')[1],'boards':{board}} for i,n in enumerate(ids)}
   stores=[Store(p/('store'+str(i)),load_pem_private_key((p/str(i)/'identity.key').read_bytes(),None),origins,{n:v for n,v in origins.items() if n!=ids[i]}) for i in range(3)]
   event=dict(version=1,origin=ids[0],board=board,article='1'*64,parent=None,revision=1,kind='post',author='alice',title='hello',body='body\r\n',created=1)
   stores[0].accept(stores[0].sign(event));errors=[]
   async def handler(reader,writer):
    try:await tls.serve(stores[0],reader,writer)
    except (ValueError,ConnectionError,asyncio.IncompleteReadError) as e:errors.append(type(e).__name__)
   server=await asyncio.start_server(handler,'127.0.0.1',0,ssl=tls.context(p/'0',[p/'1/identity.crt',p/'2/identity.crt'],True))
   port=server.sockets[0].getsockname()[1]
   try:
    ctx=tls.context(p/'1',[p/'0/identity.crt'])
    self.assertEqual(await tls.pull(stores[1],ids[0],'127.0.0.1',port,ctx),1)
    self.assertEqual(await tls.pull(stores[1],ids[0],'127.0.0.1',port,ctx),0)
    with self.assertRaises(ValueError):await tls.pull(stores[1],ids[2],'127.0.0.1',port,ctx)
    # A certificate can be TLS-valid but not an allowed direct peer.
    saved=stores[0].peers.pop(ids[2])
    with self.assertRaises((ConnectionError,asyncio.IncompleteReadError)):
     await tls.pull(stores[2],ids[0],'127.0.0.1',port,tls.context(p/'2',[p/'0/identity.crt']))
    stores[0].peers[ids[2]]=saved
    self.assertEqual(len(stores[1].pending()),1)
   finally:
    server.close();await server.wait_closed()
    for s in stores:s.close()

if __name__=='__main__':unittest.main()
