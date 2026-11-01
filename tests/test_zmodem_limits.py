"""Actual external-engine transfer at the expanded byte/batch boundaries."""
import pathlib,sys,tempfile,hashlib
from test_zmodem import peer
from zmodem_connector import ZmodemProcess,staged_files,TransferFailure

def digest(p):
 with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def run(receiver,sender):
 receiver=str(pathlib.Path(receiver).resolve());sender=str(pathlib.Path(sender).resolve())
 engine=ZmodemProcess(receiver,sender,300)
 with tempfile.TemporaryDirectory(prefix='zmodem-large-') as name:
  p=pathlib.Path(name);src=p/'source';dst=p/'received';src.mkdir();dst.mkdir()
  with (src/'large.bin').open('wb') as f:f.truncate(1024*1024*1024)
  result=peer(sender,['--','large.bin'],src,lambda fd:engine.receive(dst,fd,fd,lambda:None))
  assert result.stat().st_size==1024*1024*1024 and digest(result)==digest(src/'large.bin')
  result.unlink();(src/'large.bin').unlink()
  names=[]
  for n in range(16):
   name=f'batch-{n:02d}.bin';names.append(name)
   with (src/name).open('wb') as f:f.truncate(16*1024*1024)
  result=peer(sender,['--']+names,src,lambda fd:engine.receive_many(dst,fd,fd,lambda:None))
  assert len(result)==16 and sum(f.stat().st_size for f in result)==256*1024*1024
  assert all(digest(f)==digest(src/f.name) for f in result)
  with result[0].open('r+b') as f:f.truncate(1024*1024*1024+1)
  try:staged_files(dst,True)
  except TransferFailure:pass
  else:raise AssertionError('Over-limit batch accepted')
  # Exact 16GiB staging boundary without a 16GiB wire run.
  for f in result:
   with f.open('r+b') as out:out.truncate(1024*1024*1024)
  assert len(staged_files(dst,True))==16
  (dst/'extra.bin').write_bytes(b'x')
  try:staged_files(dst,True)
  except TransferFailure:pass
  else:raise AssertionError('Seventeenth file accepted')
 print('PASS: actual ZMODEM 1GiB file and 16-file/256MiB batch; SHA-256 matches; over-1GiB rejected; 16GiB staging accepted and seventeenth file rejected')
if __name__=='__main__':run(*sys.argv[1:])
