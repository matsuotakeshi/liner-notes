"""Operating quotas preserve old data; checkpoints follow displayed results."""
from pathlib import Path
import sys,tempfile,subprocess,sqlite3,time,os,hashlib
from test_board import Server,post,read
from test_runtime import Client
from test_telnet import Client as Telnet
from test_members import auth,PASSWORD
from test_stage58 import cmd
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from transfer_connectors import LineConnector,RemoteError

def run(binary,ctl):
 binary,ctl=map(lambda p:str(Path(p).resolve()),(binary,ctl))
 with tempfile.TemporaryDirectory(prefix='limits-check67-') as tmp:
  p=Path(tmp);board=p/'notes';members=p/'members';v=p/'vault';db=p/'liner.sqlite';limits=p/'limits';admins=p/'admins'
  subprocess.run([ctl,'init',str(v)],check=True,capture_output=True)
  s=Server(binary,board,members=members)
  c=Client(s.port);auth(c,'admin',True);post(c,'large old body','x'*400)
  for n in range(4):post(c,f'checkpoint {n}','body')
  c.close();c=Client(s.port);auth(c,'bob',True);c.close();s.close();admins.write_text('1\n')
  limits.write_text('members=2\nboards=2\nbody_bytes=20\nfile_bytes=4\nbatch_files=1\nbatch_bytes=4\nconnections=8\ntransfers=1\ninput_bytes=2048\n')
  options=['--database',str(db),'--limits',str(limits)]
  s=Server(binary,board,members=members,vault=v,admins=admins,options=options);c=Client(s.port);api=LineConnector('127.0.0.1',s.port,'admin',PASSWORD)
  try:
   auth(c,'admin');assert read(c,1)[2]=='x'*400+'\r\n'
   assert cmd(c,'POST too large').startswith(b'EDIT');c.send(b'x'*30+b'\n');assert cmd(c,'.')==b'ERR body limit';cmd(c,'/CANCEL')
   assert cmd(c,'BOARDSET 3|1|member|member|too many|info').startswith(b'ERR ')
   try:api.begin(5,hashlib.sha256(b'abcde').hexdigest(),'too-big','description')
   except RemoteError:pass
   else:raise AssertionError('file quota bypassed')
   ident=api.begin(4,hashlib.sha256(b'abcd').hexdigest(),'fits','description');api.write(ident,0,b'abcd');api.finish(ident)
  finally:c.close();api.close();s.close()
  # Auto check waits for the entire display; cancelling a final page does not commit.
  s=Server(binary,board,members=members,menu=True,telnet=True,language='en',options=options);c=Telnet(s.port)
  try:
   c.until();c.cmd('LOGIN bob',b'> ');c.cmd(PASSWORD);c.cmd('PAGER ON');c.cmd('PAGER SIZE 80 8')
   c.cmd('CHECK ARTICLES HERE',b'[Enter/Q/OFF]> ');c.cmd('Q');time.sleep(.2)
   with sqlite3.connect(db) as sql:assert sql.execute('select count(*) from news_scoped where user=2').fetchone()==(0,)
   c.cmd('PAGER OFF');c.cmd('CHECK ARTICLES HERE');time.sleep(.3)
   with sqlite3.connect(db) as sql:
    row=sql.execute('select scope,stamp from news_scoped where user=2').fetchall();assert len(row)==1 and row[0][0]=='1:0' and row[0][1]>0,row
  finally:c.close();s.close()
  # All engines see the same staging quotas; no large allocation required.
  code='from pathlib import Path; from zmodem_connector import staged_files,TransferFailure; p=Path("batch");p.mkdir();(p/"a").write_bytes(b"abcd");staged_files(p,True);(p/"b").write_bytes(b"x");\ntry:staged_files(p,True)\nexcept TransferFailure:pass\nelse:raise AssertionError("batch quota")'
  env=dict(os.environ,PYTHONPATH=str(ROOT/'scripts'),LINER_LIMIT_FILE='4',LINER_LIMIT_BATCH_FILES='1',LINER_LIMIT_BATCH_BYTES='4')
  subprocess.run([sys.executable,'-c',code],cwd=p,env=env,check=True)
 print('PASS stage67 quotas preserve existing data; scoped auto checkpoint waits for display and respects cancellation')
if __name__=='__main__':run(*sys.argv[1:])
