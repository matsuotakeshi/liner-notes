"""Real sockets: receipts, online policies, private usage logs and crash recovery."""
import pathlib,sys,tempfile,subprocess,time,hashlib,importlib.util,os,signal
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_ui import Menu
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'scripts'))
from transfer_connectors import LineConnector,BinaryConnector,RemoteError

def cmd(c,text):c.send((text+'\n').encode());return c.line()
def rows(c,text,end=b'END NEXT'):
 c.send((text+'\n').encode());out=[]
 while True:
  row=c.line();out.append(row)
  if row.startswith(end):return out
def stats(c,text='USERSTATS'):
 data=rows(c,text,b'END USERSTATS')[0].split();return int(data[3]),int(data[5])
def counts(c,ident=1):
 data=rows(c,f'FILESTATS {ident}');return int(next(x for x in data if x.startswith(b'DOWNLOADCOUNT ')).split()[2])
def denied(method):
 try:method()
 except RemoteError:return
 raise AssertionError('expected server denial')
def receive(c,ident):
 size,digest=c.begin_download(ident);data=bytearray()
 while len(data)<size:data.extend(c.read_download(ident,len(data)))
 assert len(data)==size and hashlib.sha256(data).hexdigest()==digest
 return c._downloads[ident]

def run(binary,ctl):
 binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
 with tempfile.TemporaryDirectory(prefix='stage58-') as directory:
  p=pathlib.Path(directory);board=p/'notes';users=p/'members';admins=p/'admins';vault=p/'vault'
  s=Server(binary,board,members=users)
  for name in ('alice','bob','carol'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close();admins.write_text('1\n')
  subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
  for name,data in [('payload',bytes(range(256))*8),('empty',b'')]:
   path=p/name;path.write_bytes(data);subprocess.run([ctl,'import',str(vault),str(path),name,'description'],check=True,capture_output=True)
  kwargs=dict(members=users,admins=admins,vault=vault,language='en')
  s=Server(binary,board,**kwargs);a,b,g=Client(s.port),Client(s.port),Client(s.port)
  x=y=z=None
  try:
   auth(a,'alice');auth(b,'bob')
   assert cmd(g,'USERLOG')==b'ERR login required'
   for text in ('USERLOG ALL','USERSTATS 1','BOARDINFO 1','BOARDSET 2|1|all|member|bad|x','BOARDLOG','FILEBOARDSET -|member|member'):
    assert cmd(b,text) in (b'ERR admin required',b'ERR vault denied',b'ERR catalog syntax'),text
   assert cmd(b,'FILEHISTORY')==b'ERR vault denied'
   before=stats(b)[0];time.sleep(1.05)
   assert stats(b)[1]>=1
   assert cmd(b,'LOGOUT')==b'LOGGED OUT';auth(b,'bob');assert stats(b)[0]==before+1
   # Failed login attempts and ADMIN checks must not count as logins.
   assert cmd(g,'LOGIN bob')==b'PASSWORD';assert cmd(g,'incorrect-password')==b'ERR authentication'
   assert stats(b)[0]==before+1
   assert cmd(a,'BOARDSET 2|1|all|member|Second|Online description')==b'BOARD UPDATED 2'
   assert cmd(a,'BOARDSET 3|2|member|l5|Child|restricted')==b'BOARD UPDATED 3'
   assert cmd(a,'BOARDSET 2|3|all|member|Cycle|bad')==b'ERR catalog syntax'
   assert cmd(a,'BOARDSET 1|2|all|member|Root|bad')==b'ERR catalog syntax'
   assert b'Second|Online description' in cmd(a,'BOARDINFO 2')
   assert cmd(b,'BOARD 2').startswith(b'BOARD 2');article=post(b,'online article','kept')
   assert cmd(b,'POST denied-later')==b'EDIT end with . or /CANCEL'
   assert cmd(a,'BOARDSET 2|1|all|none|Read only|changed')==b'BOARD UPDATED 2'
   b.send(b'body\n.\n');assert b'posting denied' in b.line()
   # Clear retained draft if the denial kept the editor open.
   cmd(b,'/CANCEL')
   history=rows(a,'BOARDLOG');assert sum(x.startswith(b'BOARDCHANGE ') for x in history)==3
   assert cmd(a,'FILEBOARDSET -|member|member')==b'FILEBOARD UPDATED'
   assert cmd(a,'FILEBOARDSET games|member|l5')==b'FILEBOARD UPDATED'
   assert cmd(a,'FILEBOARDINFO games')==b'FILEBOARDSETTING games|member|l5'
   assert cmd(b,'FILEBOARDINFO games')==b'FILEBOARDSETTING games|member|l5'  # readable board guide is member-visible
   x=LineConnector('127.0.0.1',s.port,'bob',PASSWORD);y=LineConnector('127.0.0.1',s.port,'carol',PASSWORD)
   x.info(1);x.read(1,0);assert counts(a)==0
   size,digest=x.begin_download(1);token=x._downloads[1][0]
   denied(lambda:x.complete_download(1))
   denied(lambda:y.protocol.request(f'FILEDONE {token} {digest}',lambda v:v.startswith(b'DOWNLOADED ')))
   denied(lambda:x.read_download(1,100))
   x.read_download(1,0);x.abort_download(1);assert counts(a)==0
   token,digest=receive(x,1);x.complete_download(1);assert counts(a)==1
   x.protocol.request(f'FILEDONE {token} {digest}',lambda v:v==b'DOWNLOADED 1');assert counts(a)==1
   receive(x,2);x.complete_download(2);assert counts(a,2)==1
   z=BinaryConnector('127.0.0.1',s.port,'bob',PASSWORD);receive(z,1);z.complete_download(1);z.close();z=None
   assert counts(a)==2
   assert cmd(a,'FILEBOARDSET -|none|none')==b'FILEBOARD UPDATED'
   denied(lambda:x.begin_download(1));assert b'DOWNLOADCOUNT' not in b'\n'.join(rows(b,'FILESTATS'))
   assert cmd(a,'FILEBOARDSET -|member|member')==b'FILEBOARD UPDATED'
   assert b'legacy-member:2' in b'\n'.join(rows(a,'FILEHISTORY 1'))
   # More than one page: stable cursors and + retain the selected file.
   for _ in range(9):receive(x,1);x.complete_download(1)
   first=rows(a,'FILEHISTORY 1');second=rows(a,'+');assert first[-1]!=b'END NEXT 0' and second[-1]==b'END NEXT 0'
   assert counts(a)==11
   expected_logins=stats(b)[0]
  finally:
   for c in (x,y,z,a,b,g):
    if c:c.close()
   s.close()
  s=Server(binary,board,**kwargs);a,b=Client(s.port),Client(s.port)
  try:
   auth(a,'alice');auth(b,'bob');assert stats(b)[0]==expected_logins+1;assert counts(a)==11
   assert b'Read only|changed' in cmd(a,'BOARDINFO 2')
   assert cmd(b,'BOARD 2').startswith(b'BOARD 2');assert cmd(b,'POST blocked')==b'ERR posting denied'
   b.send(f'READ {article}\n'.encode());assert b'ARTICLE' in b.line()
   # Receipt retry remains idempotent after restart.
   x=LineConnector('127.0.0.1',s.port,'bob',PASSWORD)
   try:x.protocol.request(f'FILEDONE {token} {digest}',lambda v:v==b'DOWNLOADED 1')
   finally:x.close()
   assert counts(a)==11
  finally:s.close(crash=True);a.close();b.close()
  s=Server(binary,board,**kwargs);a=Client(s.port)
  try:
   auth(a,'alice');data=b'\n'.join(rows(a,'USERLOG ALL'));assert b' X ' in data,data
  finally:a.close();s.close()
  # Newly introduced journals reject damaged tails, preserving originals.
  for path in (pathlib.Path(str(board)+'.catalog'),pathlib.Path(str(users)+'.userlog'),vault/'downloads.dat'):
   original=path.read_bytes();path.write_bytes(original+b'broken');bad=path.read_bytes()
   args=[binary,'--vault-dir',str(vault),'--admins',str(admins),'0',str(board),str(users)]
   result=subprocess.run(args,capture_output=True,timeout=8);assert result.returncode!=0 and b'LISTEN' not in result.stdout;assert path.read_bytes()==bad
   path.write_bytes(original)
 if os.name=='posix':
  import fault_io as resource
  with tempfile.TemporaryDirectory(prefix='userlog-failure-') as directory:
   p=pathlib.Path(directory)
   def limited():
    signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(128,128))
   s=Server(binary,p/'notes',members=p/'users',preexec_fn=limited)
   import socket
   c=socket.create_connection(('127.0.0.1',s.port),timeout=3)
   try:assert c.recv(100)==b''
   finally:c.close()
   assert s.proc.wait(timeout=5)!=0;assert 'userlog:' in s.proc.stderr.read()
   path=p/'users.userlog';bad=path.read_bytes();assert len(bad)==128
   check=subprocess.run([binary,'0',str(p/'notes'),str(p/'users')],capture_output=True,timeout=8)
   assert check.returncode and b'userlog:' in check.stderr and path.read_bytes()==bad
 print('PASS: download receipts/counts/idempotence/privacy/partial and binary paths; online board policies/ACL/draft recheck/restart; USERLOG privacy/login counts/usage/crash and corrupt-tail refusal')
if __name__=='__main__':run(*sys.argv[1:])
