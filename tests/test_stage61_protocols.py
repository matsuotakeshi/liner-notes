"""Real lrzsz XY/G peers and independent RTBBS M-LINK/text fixtures."""
import pathlib,sys,tempfile,socket,threading,hashlib,subprocess,os
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from zmodem_connector import ZmodemProcess,TransferFailure
from legacy_transfer import LegacyEngine
from test_zmodem import peer

def fixture(action,partner):
 a,b=socket.socketpair();errors=[]
 def worker():
  try:partner(b)
  except BaseException as e:errors.append(e)
  finally:b.close()
 t=threading.Thread(target=worker);t.start()
 try:result=action(a.fileno());t.join(5);assert not t.is_alive();assert not errors,errors;return result
 finally:a.close();t.join(2)

def exact(s,n):
 data=b''
 while len(data)<n:
  chunk=s.recv(n-len(data));assert chunk;data+=chunk
 return data

def run(receiver,sender):
 receiver,sender=[str(pathlib.Path(x).resolve()) for x in (receiver,sender)]
 with tempfile.TemporaryDirectory(prefix='protocol61-') as d:
  p=pathlib.Path(d);source=p/'source';source.mkdir();payload=bytes(range(256))*23+b'last\x1a';name='PAYLOAD.BIN';path=source/name;path.write_bytes(payload)
  for mode in ('x','y'):
   target=p/('receive-'+mode);target.mkdir();engine=ZmodemProcess(receiver,sender,15,protocol=mode,filename=name,expected_size=len(payload))
   received=peer(sender,['--xmodem' if mode=='x' else '--ymodem','--',name],source,lambda fd:engine.receive(target,fd,fd,lambda:None));assert received.read_bytes()==payload
   target=p/('send-'+mode);target.mkdir()
   peer(receiver,(['--xmodem','--with-crc','--',name] if mode=='x' else ['--ymodem','--protect']),target,lambda fd:engine.send(path,fd,fd,lambda:None))
   got=(target/name.lower() if mode=='y' and not (target/name).exists() else target/name).read_bytes();assert got[:len(payload)]==payload and all(x==26 for x in got[len(payload):])
  # lsz is an independent YMODEM-g sender. Receive verifies CRC and sequence.
  target=p/'receive-g';target.mkdir();engine=LegacyEngine('g',15)
  received=peer(sender,['--ymodem','--',name],source,lambda fd:engine.receive(target,fd,fd,lambda:None));assert received.read_bytes()==payload
  # Independent strict streaming receiver validates every block of the sender.
  import binascii
  def g_receiver(s):
   def packet():
    start=exact(s,1);n=128 if start==b'\x01' else 1024 if start==b'\x02' else 0;assert n
    seq,inv=exact(s,2);data=exact(s,n);crc=int.from_bytes(exact(s,2),'big');assert seq^inv==255 and binascii.crc_hqx(data,0)==crc;return seq,data
   s.sendall(b'G');seq,header=packet();assert seq==0 and header.startswith(name.encode()+b'\0');s.sendall(b'G');got=b'';seq=1
   while len(got)<len(payload):
    num,data=packet();assert num==seq;got+=data;seq=(seq+1)&255
   assert got[:len(payload)]==payload;assert exact(s,1)==b'\x04';s.sendall(b'\x06G');num,data=packet();assert num==0 and not any(data)
  fixture(lambda fd:engine.send(path,fd,fd,lambda:None),g_receiver)
  target=p/'receive-m';target.mkdir();engine=LegacyEngine('m',5)
  def m_sender(s):
   assert exact(s,1)==b'M';s.sendall(f'{name} {len(payload)} 202610310100\r'.encode());assert exact(s,1)==b'M';s.sendall(payload);assert exact(s,1)==b'E';s.sendall(b'\r');assert exact(s,1)==b'E'
  got=fixture(lambda fd:engine.receive(target,fd,fd,lambda:None),m_sender);assert got.read_bytes()==payload
  def m_receiver(s):
   s.sendall(b'M');header=b''
   while not header.endswith(b'\r'):header+=exact(s,1)
   fields=header.split();assert fields[0]==name.encode() and int(fields[1])==len(payload);s.sendall(b'M');assert exact(s,len(payload))==payload;s.sendall(b'E');assert exact(s,1)==b'\r'
  fixture(lambda fd:engine.send(path,fd,fd,lambda:None),m_receiver)
  text=b'Mixed text\r\nsecond line\r\n';(source/name).write_bytes(text);engine=LegacyEngine('t',5,filename=name,expected_size=len(text));target=p/'receive-t';target.mkdir()
  def text_sender(s):s.sendall(text+b'\x1a')
  got=fixture(lambda fd:engine.receive(target,fd,fd,lambda:None),text_sender);assert got.read_bytes()==text
  def text_receiver(s):assert exact(s,len(text)+1)==text+b'\x1a'
  fixture(lambda fd:engine.send(path,fd,fd,lambda:None),text_receiver)
 print('PASS real lrzsz X/Y send/receive, YMODEM-g sender, independent G/M-LINK/text byte fixtures')
if __name__=='__main__':run(*sys.argv[1:])
