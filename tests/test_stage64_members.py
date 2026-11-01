"""Real maximum member-ID load with SQLite eviction and permanently reserved IDs."""
import sys,os,json,tempfile,subprocess,shutil,time
from pathlib import Path
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_stage58 import cmd,rows
from test_stage63_memory import rss

def run(binary,generator):
 binary,generator=[str(Path(x).resolve()) for x in (binary,generator)]
 os.environ['LINER_TEST_START_TIMEOUT']='180'
 with tempfile.TemporaryDirectory(prefix='stage64-million-') as tmp:
  p=Path(tmp);s=Server(binary,p/'notes',members=p/'seed');c=Client(s.port)
  try:auth(c,'alice',True);baseline=rss(s.proc)
  finally:c.close();s.close()
  subprocess.run([generator,str(p/'seed'),str(p/'members'),'999999'],check=True)
  (p/'admins').write_text('1\n');started=time.monotonic()
  s=Server(binary,p/'notes',members=p/'members',admins=p/'admins');a=Client(s.port);c=Client(s.port)
  try:
   auth(a,'alice');auth(c,'u999999');loaded=rss(s.proc)
   assert loaded-baseline<64*1024*1024,(baseline,loaded)
   assert cmd(c,'ACCOUNT')==b'ACCOUNT 999999 u999999'
   assert cmd(a,'BOARDSET 1024|1|member|member|Highest|last')==b'BOARD UPDATED 1024'
   assert cmd(c,'BOARD 1024').startswith(b'BOARD 1024');assert post(c,'last-author','body')==1
   assert read(c,1)[2]=='body\r\n';assert cmd(c,'SEEN 1')==b'SEEN 1'
   assert cmd(c,'PROFILESET MACHINE FreeBSD')==b'PROFILE SAVED'
   assert cmd(c,'RETIRE CONFIRM').startswith((b'MEMBER REMOVED',b'ERR session revoked'))
   c.close();c=Client(s.port);assert cmd(c,'REGISTER next')==b'PASSWORD';assert cmd(c,PASSWORD)==b'ERR authentication'
  finally:a.close();c.close();s.close()
  s=Server(binary,p/'notes',members=p/'members',admins=p/'admins');a=Client(s.port)
  try:
   auth(a,'alice');assert cmd(a,'BOARD 1024').startswith(b'BOARD 1024');assert read(a,1)[2]=='body\r\n'
   assert cmd(a,'MEMBER 999999').startswith(b'MEMBER')
  finally:a.close();s.close()
  print(json.dumps(dict(members=999999,baseline_rss=baseline,loaded_rss=loaded,growth=loaded-baseline,elapsed_seconds=round(time.monotonic()-started,2))))
 print('PASS stage64 member 999999, board 1024, SQLite eviction, high-ID post/seen/profile/retirement, reserved capacity and restart')
if __name__=='__main__':run(*sys.argv[1:])
