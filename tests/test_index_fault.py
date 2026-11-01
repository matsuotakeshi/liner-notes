"""A real process file-size quota refuses SQLite startup without touching originals."""
import sys,tempfile,subprocess,os,signal
from pathlib import Path
from test_board import Server,post
from test_runtime import Client
from test_members import auth

def run(binary):
 if os.name!='posix':return
 import resource
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='index-fault-') as tmp:
  p=Path(tmp);board=p/'notes';members=p/'members';s=Server(binary,board,members=members);c=Client(s.port)
  try:auth(c,'alice',True);post(c,'original','unchanged')
  finally:c.close();s.close()
  before={x:x.read_bytes() for x in p.iterdir() if x.is_file() and '-index.sqlite' not in x.name and not x.name.endswith('.userlog')}
  def limit():
   signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(1024,1024))
  r=subprocess.run([binary,'--user-log','-','0',str(board),str(members)],capture_output=True,preexec_fn=limit,timeout=20)
  assert r.returncode and b'LISTEN' not in r.stdout and b'SQLite working index failed' in r.stderr,(r.returncode,r.stderr)
  assert all(x.read_bytes()==content for x,content in before.items())
 print('PASS: real file-size quota fails SQLite startup closed; original journals unchanged')
if __name__=='__main__':run(sys.argv[1])
