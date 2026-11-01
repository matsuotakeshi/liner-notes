"""Replace both client connectors with memory I/O: no server or wire codec."""
import hashlib
import pathlib
import tempfile
from test_network_transfer import uploader, downloader
from transfer_connectors import TransferConnector, RemoteError


class MemoryConnector(TransferConnector):
    name='memory-test'
    chunk_size=257
    payload=bytearray()
    def __init__(self,*args):pass
    def close(self):pass
    def begin(self,size,digest,name,description):
        type(self).expected=(size,digest);type(self).payload=bytearray();return 1
    def status(self,ident):return 'ACTIVE',len(self.payload),*self.expected
    def write(self,ident,offset,data):
        assert offset==len(self.payload) and len(data)<=257;self.payload.extend(data);return len(self.payload)
    def finish(self,ident):
        actual=(len(self.payload),hashlib.sha256(self.payload).hexdigest());assert actual==self.expected;return actual
    def info(self,ident):return self.expected
    def read(self,ident,offset):return bytes(self.payload[offset:offset+1500])
    def cancel(self,ident):self.payload.clear()


with tempfile.TemporaryDirectory() as directory:
    p=pathlib.Path(directory);source=p/'source';source.write_bytes(bytes(range(256))*20)
    result=uploader.upload('unused',1,'member','not-a-credential',source,p/'state',connector_factory=MemoryConnector)
    downloader.download('unused',1,'member','not-a-credential',result[0],p/'received',connector_factory=MemoryConnector)
    assert (p/'received').read_bytes()==source.read_bytes()
    class Refused(MemoryConnector):
        def begin(self,*args):raise RemoteError('ERR vault limit')
    try:uploader.upload('unused',1,'member','not-a-credential',source,p/'refused',connector_factory=Refused)
    except RemoteError:pass
    else:raise AssertionError('refusal was ignored')
    assert uploader.StateFile(p/'refused').data['phase']=='prepared'
print('PASS: upload/download workflows accept an independent connector and chunk sizing; explicit refusal permits a safe retry')
