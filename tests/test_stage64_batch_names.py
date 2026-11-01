"""Duplicate attachment names stay unique, UTF-8-valid and within 100 bytes."""
import sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from zmodem_host import batch_path
with tempfile.TemporaryDirectory(prefix='batch-names-') as tmp:
 root=Path(tmp)
 for original in ['same.txt','a'*100,'あ'*33]:
  (root/original).write_bytes(b'original')
  first=batch_path(root,original,123);first.write_bytes(b'first')
  second=batch_path(root,original,123);second.write_bytes(b'second')
  assert first!=second and len(first.name.encode())<=100 and len(second.name.encode())<=100
  assert (root/original).read_bytes()==b'original'
print('PASS: duplicate/max-byte UTF-8 attachment names and prefix collisions preserve all files')
