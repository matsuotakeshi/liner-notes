#!/usr/bin/env python3
"""Prove actual ZCRC resume on a partial file, optionally write a private receipt."""
import argparse,hashlib,json,pathlib,select,socket,subprocess,tempfile,time,os,stat,fcntl,uuid

def save_receipt(path,receipt):
 """Publish only verified results; preserve a previous receipt before replacement."""
 path=pathlib.Path(path).absolute()
 directory=os.open(path.parent,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
 temporary='.resume-'+uuid.uuid4().hex
 data=(json.dumps(receipt,sort_keys=True)+'\n').encode()
 try:
  fcntl.flock(directory,fcntl.LOCK_EX)
  previous=None;owner=None
  try:
   fd=os.open(path.name,os.O_RDONLY|os.O_NOFOLLOW,dir_fd=directory)
  except FileNotFoundError:pass
  else:
   with os.fdopen(fd,'rb') as source:
    st=os.fstat(source.fileno())
    if not stat.S_ISREG(st.st_mode) or st.st_nlink!=1 or st.st_mode&0o077 or st.st_size>8192 or (os.geteuid()!=0 and st.st_uid!=os.geteuid()):raise ValueError('Unsafe existing receipt; unchanged')
    previous=source.read();owner=(st.st_uid,st.st_gid)
   try:equal=json.loads(previous)==receipt
   except (ValueError,UnicodeError):equal=False
   if equal:return 'unchanged'
  def write(name,content):
   fd=os.open(name,os.O_WRONLY|os.O_CREAT|os.O_EXCL|os.O_NOFOLLOW,0o600,dir_fd=directory)
   with os.fdopen(fd,'wb') as target:
    if owner is not None and os.geteuid()==0:os.fchown(target.fileno(),*owner)
    target.write(content);target.flush();os.fsync(target.fileno())
  write(temporary,data)
  if previous is not None:
   write(path.name+'.bak-'+time.strftime('%Y%m%d-%H%M%S')+'-'+uuid.uuid4().hex[:8],previous)
   os.fsync(directory)
  os.replace(temporary,path.name,src_dir_fd=directory,dst_dir_fd=directory)
  os.fsync(directory)
  return 'updated' if previous is not None else 'created'
 finally:
  try:os.unlink(temporary,dir_fd=directory)
  except FileNotFoundError:pass
  os.close(directory)

def check(receiver,sender):
 receiver=str(pathlib.Path(receiver).resolve());sender=str(pathlib.Path(sender).resolve())
 before=hashlib.sha256(pathlib.Path(receiver).read_bytes()).hexdigest()
 payload=bytes(range(256))*4096
 with tempfile.TemporaryDirectory(prefix='lrzsz-resume-') as name:
  p=pathlib.Path(name);src=p/'send';dst=p/'recv';src.mkdir();dst.mkdir()
  (src/'sample.bin').write_bytes(payload);(dst/'sample.bin').write_bytes(payload[:786432])
  sa,sb=socket.socketpair();ra,rb=socket.socketpair()
  tx=subprocess.Popen([sender,'--binary','--quiet','--escape','--syslog=off','--','sample.bin'],cwd=src,stdin=sb,stdout=sb,stderr=subprocess.DEVNULL)
  rx=subprocess.Popen([receiver,'--binary','--quiet','--escape','--syslog=off','--resume','--resume'],cwd=dst,stdin=rb,stdout=rb,stderr=subprocess.DEVNULL)
  sb.close();rb.close();sa.setblocking(False);ra.setblocking(False)
  buffers={sa:bytearray(),ra:bytearray()};open_read={sa,ra};wire=0;deadline=time.monotonic()+30
  try:
   while (tx.poll() is None or rx.poll() is None or any(buffers.values())) and time.monotonic()<deadline:
    reads,writes,_=select.select(list(open_read),[s for s,b in buffers.items() if b],[],.02)
    for s in writes:
     try:n=s.send(buffers[s]);del buffers[s][:n]
     except BrokenPipeError:buffers[s].clear()
    for s in reads:
     chunk=s.recv(65536)
     if not chunk:open_read.remove(s);continue
     if s is sa:wire+=len(chunk)
     buffers[ra if s is sa else sa]+=chunk
   if tx.wait(timeout=2)!=0 or rx.wait(timeout=2)!=0:raise ValueError('Transfer failed')
   if (dst/'sample.bin').read_bytes()!=payload:raise ValueError('Completed content mismatch')
   if not 0<wire<len(payload):raise ValueError(f'No real resume: sender wrote {wire} for {len(payload)} bytes')
  finally:
   sa.close();ra.close()
   for proc in (tx,rx):
    if proc.poll() is None:proc.kill()
    proc.wait()
 if hashlib.sha256(pathlib.Path(receiver).read_bytes()).hexdigest()!=before:raise ValueError('Receiver changed during test')
 return dict(patch='crc32-lp64-v1',sha256=before),dict(payload_bytes=len(payload),retained_bytes=786432,sender_wire_bytes=wire,sha256=hashlib.sha256(payload).hexdigest())
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('receiver');p.add_argument('sender');p.add_argument('--receipt');a=p.parse_args()
 try:
  receipt,result=check(a.receiver,a.sender)
  if a.receipt:result['receipt_status']=save_receipt(a.receipt,receipt)
 except (OSError,ValueError,subprocess.SubprocessError) as error:p.exit(1,str(error)+'\n')
 print(json.dumps(result,sort_keys=True))
