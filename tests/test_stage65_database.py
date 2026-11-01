from pathlib import Path
import sys,tempfile,sqlite3,subprocess
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd,rows

def run(binary,ctl):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='db65-') as tmp:
  p=Path(tmp);board=p/'notes';members=p/'members'
  def start():return Server(binary,board,members=members)
  s=start();a=Client(s.port)
  try:
   auth(a,'alice',True)
   for i in range(140):post(a,'cache '+str(i),'body needle '+str(i))
   assert cmd(a,'MAIL SEND alice cached').startswith(b'MAIL EDIT');a.send(b'mail-body\n');assert cmd(a,'.').startswith(b'MAIL SENT')
  finally:a.close();s.close()
  for suffix in ('.article-index.sqlite',):
   with sqlite3.connect(str(board)+suffix) as db:assert db.execute('select format from checkpoint').fetchone()==(6501,)
  for _ in range(2):
   s=start();a=Client(s.port)
   try:
    auth(a,'alice');assert read(a,140)[2]=='body needle 139\r\n'
    result=rows(a,'SEARCH needle');assert any(b'cache ' in r for r in result)
    assert b'mail-body' in rows(a,'MAIL READ 1',b'MAIL END')
   finally:a.close();s.close()
  # Journal append followed by a crash cannot reuse the prior cache.
  s=start();a=Client(s.port);auth(a,'alice');assert post(a,'after checkpoint','crash safe')==141;a.close();s.close(crash=True)
  s=start();a=Client(s.port)
  try:auth(a,'alice');assert read(a,141)[2]=='crash safe\r\n'
  finally:a.close();s.close()
  # Equal-length corruption invalidates the signature, then normal validation rejects it.
  raw=bytearray(board.read_bytes());raw[-1]^=1;board.write_bytes(raw)
  result=subprocess.run([binary,'0',str(board),str(members)],capture_output=True,timeout=15);assert result.returncode!=0,result
  assert board.read_bytes()==raw
 print('stage65 SQL cache: reuse, eviction, mail, crash replay, tamper rejection passed')
if __name__=='__main__':run(*sys.argv[1:])
