"""Actual 1GiB vault import/hash, over-limit rejection and reclaimed ID reservation."""
from pathlib import Path
import sys,tempfile,subprocess,hashlib

def run(ctl):
 ctl=str(Path(ctl).resolve())
 with tempfile.TemporaryDirectory(prefix='vault-gib-') as d:
  p=Path(d);vault=p/'vault';source=p/'source'
  def call(*args,ok=True):
   out=subprocess.run([ctl]+list(map(str,args)),capture_output=True,timeout=180)
   assert (out.returncode==0)==ok,(args,out.stderr)
   return out.stdout
  call('init',vault)
  with source.open('wb') as f:f.truncate(1024**3)
  receipt=call('import',vault,source,'one-gib.bin','boundary');assert receipt.startswith(b'1 '),receipt
  blob=vault/'00000001.bin';assert blob.stat().st_size==1024**3
  with source.open('rb') as f:want=hashlib.file_digest(f,'sha256').hexdigest()
  with blob.open('rb') as f:assert hashlib.file_digest(f,'sha256').hexdigest()==want
  assert want.encode() in receipt
  before=(vault/'index.dat').read_bytes()
  with source.open('ab') as f:f.write(b'x')
  call('import',vault,source,'oversize.bin','bad',ok=False);assert (vault/'index.dat').read_bytes()==before
  call('delete',vault,1,'CONFIRM');assert not blob.exists()
  source.write_bytes(b'next');assert call('import',vault,source,'next.bin','').startswith(b'2 ')
 print('PASS: 1GiB vault import/SHA256, 1GiB+1 rejection without commit, physical reclamation, monotonic ID')
if __name__=='__main__':run(sys.argv[1])
