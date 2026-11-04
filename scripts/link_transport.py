"""Mutual TLS transport with explicit public-key peer identity and signed events."""
import asyncio,datetime,json,os,ssl,struct
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.x509.oid import NameOID,ExtendedKeyUsageOID
from link_store import canonical,identity,public_bytes

FRAME_MAX=4*1024*1024

def generate(directory,name):
    directory=Path(directory);directory.mkdir(mode=0o700)
    key=Ed25519PrivateKey.generate();subject=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,name)])
    now=datetime.datetime.now(datetime.timezone.utc)
    cert=(x509.CertificateBuilder().subject_name(subject).issuer_name(subject).public_key(key.public_key())
          .serial_number(x509.random_serial_number()).not_valid_before(now-datetime.timedelta(minutes=5))
          .not_valid_after(now+datetime.timedelta(days=3650))
          .add_extension(x509.BasicConstraints(ca=True,path_length=0),True)
          .add_extension(x509.KeyUsage(True,False,False,False,False,True,True,False,False),True)
          .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH,ExtendedKeyUsageOID.CLIENT_AUTH]),False)
          .sign(key,None))
    for filename,data in (('identity.key',key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption())),('identity.crt',cert.public_bytes(serialization.Encoding.PEM))):
        fd=os.open(directory/filename,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
        with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
    return identity(key.public_key())

def certificate_node(path):
    key=x509.load_pem_x509_certificate(Path(path).read_bytes()).public_key()
    return identity(key),public_bytes(key)

def context(directory,certificates,server=False):
    ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER if server else ssl.PROTOCOL_TLS_CLIENT)
    ctx.minimum_version=ssl.TLSVersion.TLSv1_3
    if not server:ctx.check_hostname=False # Peer identity is the registered public key below.
    ctx.verify_mode=ssl.CERT_REQUIRED
    ctx.load_cert_chain(str(Path(directory)/'identity.crt'),str(Path(directory)/'identity.key'))
    for path in certificates:ctx.load_verify_locations(cafile=str(path))
    return ctx

def authenticated_peer(writer,allowed,expected=None):
    tls=writer.get_extra_info('ssl_object')
    if not tls:raise ValueError('TLS is required')
    cert=x509.load_der_x509_certificate(tls.getpeercert(binary_form=True));node=identity(cert.public_key())
    if node not in allowed or expected is not None and node!=expected:raise ValueError('Unregistered peer public key')
    return node

def unique(pairs):
    value={}
    for k,v in pairs:
        if k in value:raise ValueError('Duplicate JSON field')
        value[k]=v
    return value

async def receive(reader):
    size=struct.unpack('!I',await reader.readexactly(4))[0]
    if not 0<size<=FRAME_MAX:raise ValueError('Link frame too large')
    raw=await reader.readexactly(size)
    try:value=json.loads(raw,object_pairs_hook=unique,parse_constant=lambda _:(_ for _ in ()).throw(ValueError('Invalid JSON constant')))
    except RecursionError as error:raise ValueError('JSON nesting is too deep') from error
    stack=[(value,0)];nodes=0
    while stack:
        item,depth=stack.pop();nodes+=1
        if depth>16 or nodes>512:raise ValueError('JSON structure is too complex')
        if isinstance(item,dict):stack.extend((v,depth+1) for v in item.values())
        elif isinstance(item,list):stack.extend((v,depth+1) for v in item)
    return value

async def send(writer,value):
    raw=canonical(value)
    if len(raw)>FRAME_MAX:raise ValueError('Link frame too large')
    writer.write(struct.pack('!I',len(raw))+raw);await writer.drain()

async def serve(store,reader,writer):
    try:
        async with asyncio.timeout(30):
            peer=authenticated_peer(writer,store.peers)
            request=await receive(reader)
            if not isinstance(request,dict) or set(request)!={'version','after'} or type(request['version']) is not int or request['version']!=1:raise ValueError('Invalid exchange request')
            events,cursor=store.batch(peer,request['after'])
            await send(writer,{'version':1,'cursor':cursor,'events':events})
    finally:
        writer.close()
        try:await writer.wait_closed()
        except (OSError,ssl.SSLError):pass

async def pull(store,peer,host,port,ctx):
    async with asyncio.timeout(30):
        reader,writer=await asyncio.open_connection(host,port,ssl=ctx,server_hostname=host)
        try:
            authenticated_peer(writer,store.peers,peer)
            after=store.cursor(peer);await send(writer,{'version':1,'after':after})
            result=await receive(reader)
            if not isinstance(result,dict) or set(result)!={'version','cursor','events'} or type(result['version']) is not int or result['version']!=1:raise ValueError('Invalid exchange response')
            if type(result['cursor']) is not int or not after<=result['cursor']<=9223372036854775807 or not isinstance(result['events'],list) or len(result['events'])>4:raise ValueError('Invalid peer cursor or batch')
            count=0
            for event in result['events']:count+=store.accept(event,peer)
            store.acknowledge(peer,result['cursor'])
            return count
        finally:
            writer.close()
            try:await writer.wait_closed()
            except (OSError,ssl.SSLError):pass
