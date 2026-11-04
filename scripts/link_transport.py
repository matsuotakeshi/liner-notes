"""Mutual TLS transport with explicit public-key peer identity and signed events."""
import asyncio,datetime,json,os,ssl,struct,tempfile
from pathlib import Path
from cryptography import x509
from cryptography.exceptions import InvalidSignature
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


def rotate_tls(directory,force=False):
    """Rotate only online TLS keys; the pinned signing identity stays stable.

    The new leaf certificate is signed by that identity. It is not a new origin
    and cannot grant any additional board permission. Call with the daemon lock.
    """
    directory=Path(directory);current=directory/'tls-current.json'
    now=datetime.datetime.now(datetime.timezone.utc)
    if current.exists() and not force:
        value=json.loads(current.read_text());stem=value['generation']
        if not isinstance(stem,str) or not stem.startswith('tls-') or '/' in stem or '\\' in stem or '..' in stem:raise ValueError('Unsafe TLS generation')
        cert=x509.load_pem_x509_certificate((directory/stem/'certificate.pem').read_bytes())
        if cert.not_valid_after_utc>now+datetime.timedelta(days=30):return directory/stem,False
    authority=x509.load_pem_x509_certificate((directory/'identity.crt').read_bytes())
    signer=serialization.load_pem_private_key((directory/'identity.key').read_bytes(),None)
    if authority.not_valid_after_utc<=now+datetime.timedelta(days=1):raise ValueError('Pinned identity certificate needs administrator renewal')
    key=Ed25519PrivateKey.generate()
    subject=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'TLS '+identity(authority.public_key())[:40])])
    cert=(x509.CertificateBuilder().subject_name(subject).issuer_name(authority.subject).public_key(key.public_key())
        .serial_number(x509.random_serial_number()).not_valid_before(now-datetime.timedelta(minutes=5))
        .not_valid_after(min(now+datetime.timedelta(days=90),authority.not_valid_after_utc))
        .add_extension(x509.BasicConstraints(ca=False,path_length=None),True)
        .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(authority.public_key()),False)
        .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()),False)
        .add_extension(x509.KeyUsage(True,False,False,False,False,False,False,False,False),True)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH,ExtendedKeyUsageOID.CLIENT_AUTH]),False).sign(signer,None))
    target=Path(tempfile.mkdtemp(prefix='tls-',dir=directory))
    for name,data in [('key.pem',key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption())),('certificate.pem',cert.public_bytes(serialization.Encoding.PEM))]:
        fd=os.open(target/name,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
        with os.fdopen(fd,'wb') as stream:stream.write(data);stream.flush();os.fsync(stream.fileno())
    fd,temp=tempfile.mkstemp(prefix='.tls-current-',dir=directory)
    with os.fdopen(fd,'w') as stream:json.dump(dict(generation=target.name),stream);stream.flush();os.fsync(stream.fileno())
    os.replace(temp,current)
    return target,True


def rotated_context(directory,certificates,server=False):
    generation,_=rotate_tls(directory)
    ctx=context(directory,certificates,server)
    ctx.load_cert_chain(str(generation/'certificate.pem'),str(generation/'key.pem'))
    return ctx

def authenticated_peer(writer,allowed,expected=None):
    tls=writer.get_extra_info('ssl_object')
    if not tls:raise ValueError('TLS is required')
    cert=x509.load_der_x509_certificate(tls.getpeercert(binary_form=True));node=identity(cert.public_key())
    if node in allowed and (expected is None or node==expected):return node
    # TLS has verified the chain. Pin the leaf's signer to the registered root
    # as well, so another trusted root cannot choose somebody else's identity.
    for pinned,policy in allowed.items():
        if expected is not None and pinned!=expected:continue
        try:
            from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
            Ed25519PublicKey.from_public_bytes(policy['public']).verify(cert.signature,cert.tbs_certificate_bytes)
            return pinned
        except (InvalidSignature,ValueError):continue
    raise ValueError('Unregistered peer public key')

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
            if store.db.execute('SELECT 1 FROM paused_peers WHERE peer=?',(peer,)).fetchone():raise ValueError('Peer exchange paused')
            request=await receive(reader)
            if not isinstance(request,dict) or set(request)!={'version','after'} or type(request['version']) is not int or request['version'] not in (1,2):raise ValueError('Invalid exchange request')
            events,cursor=store.batch(peer,request['after'])
            if request['version']==1 and any(e['event']['version']==2 for e in events):raise ValueError('Peer must support link protocol 2')
            await send(writer,{'version':request['version'],'cursor':cursor,'events':events})
    finally:
        writer.close()
        try:await writer.wait_closed()
        except (OSError,ssl.SSLError):pass

async def pull(store,peer,host,port,ctx):
    async with asyncio.timeout(30):
        reader,writer=await asyncio.open_connection(host,port,ssl=ctx,server_hostname=host)
        try:
            authenticated_peer(writer,store.peers,peer)
            after=store.cursor(peer);await send(writer,{'version':2,'after':after})
            result=await receive(reader)
            if not isinstance(result,dict) or set(result)!={'version','cursor','events'} or type(result['version']) is not int or result['version']!=2:raise ValueError('Invalid exchange response')
            if type(result['cursor']) is not int or not after<=result['cursor']<=9223372036854775807 or not isinstance(result['events'],list) or len(result['events'])>4:raise ValueError('Invalid peer cursor or batch')
            count=0
            for event in result['events']:count+=store.accept(event,peer)
            store.acknowledge(peer,result['cursor'])
            return count
        finally:
            writer.close()
            try:await writer.wait_closed()
            except (OSError,ssl.SSLError):pass
