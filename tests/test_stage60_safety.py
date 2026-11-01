"""Inherited passwords/ACLs, SIGOP scope, exact INFO limits, and write failures."""
import os,pathlib,shutil,subprocess,sys,tempfile,time,importlib.util
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd,rows
from test_stage60 import password

def run(binary):
 binary=str(pathlib.Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='stage60-safety-') as d:
  p=pathlib.Path(d);board=p/'notes';users=p/'users';admins=p/'admins'
  s=Server(binary,board,members=users)
  try:
   for name in ('admin','sigop','alice'):
    c=Client(s.port)
    try:auth(c,name,True)
    finally:c.close()
  finally:s.close()
  admins.write_text('1\n');kw=dict(members=users,admins=admins)
  s=Server(binary,board,**kw);a=Client(s.port);auth(a,'admin')
  try:
   for b,parent,acl in ((2,1,'all'),(3,2,'all'),(4,1,'all'),(5,1,'u3')):
    assert cmd(a,f'BOARDSET {b}|{parent}|{acl}|member|Board{b}|guide')==f'BOARD UPDATED {b}'.encode()
   assert cmd(a,'SIGOP 2 2')==b'SIGOP SET 2 2'
   password(a,2,'Parent-Secret');password(a,3,'Child-Secret');password(a,5,'Acl-Secret')
   c,b,g=Client(s.port),Client(s.port),Client(s.port)
   try:
    auth(c,'alice');auth(b,'sigop');time.sleep(1.05)
    assert cmd(c,'board 3')==b'BOARD PASSWORD'
    assert cmd(c,'Child-Secret')==b'BOARD PASSWORD' # nearest protected ancestor first
    assert cmd(c,'Parent-Secret').startswith(b'BOARD 3')
    ident=post(c,'Child post','Scoped Body')
    assert b'alice' in b'\n'.join(rows(c,'profile alice',b'END PROFILE'))
    assert cmd(g,'BOARD 5')==b'ERR board unavailable'
    assert cmd(b,'BOARDPASS 4')==b'ERR admin required'
    assert cmd(b,'INFOEDIT 4')==b'ERR admin required'
    assert cmd(b,'BOARDACCESS 4')==b'ERR admin required'
    assert cmd(b,'BOARDOPTIONS 2 BOARD 90 0 0 3')==b'BOARD OPTIONS SAVED'
    assert cmd(b,'BOARDDELETE 2 CONFIRM')==b'ERR admin required'
    assert cmd(b,'BOARD 2').startswith(b'BOARD 2') # inherited SIGOP bypass
    assert cmd(b,'BOARD 3').startswith(b'BOARD 3');assert read(b,1)[2]=='Scoped Body\r\n' # inherited child management
    # Parent unlock doesn't imply child unlock after the child's password changes.
    password(a,3,'Changed-Child')
    assert cmd(c,f'READ {ident}')==b'ERR board unavailable'
    assert cmd(c,'BOARD 3')==b'BOARD PASSWORD'
    assert cmd(c,'Changed-Child').startswith(b'BOARD 3')
    assert read(c,ident)[2]=='Scoped Body\r\n'
    # Sibling order changes traversal, but root remains first.
    listing=rows(a,'BOARDS');sequence=[int(x.strip().split()[0][1:]) for x in listing if x.strip().startswith(b'#')]
    assert sequence.index(4)<sequence.index(2),listing
    # INFO accepts exactly 8192 bytes including CRLF, then rejects overflow without replacing it.
    assert cmd(b,'INFOEDIT 2')==b'BOARD INFO EDIT'
    for _ in range(4):b.send(b'x'*2046+b'\r\n')
    assert cmd(b,'/save')==b'BOARD INFO SAVED'
    assert cmd(b,'INFOEDIT 2')==b'BOARD INFO EDIT';b.send(b'overflow\r\n')
    assert cmd(b,'/save')==b'ERR board info limit';assert cmd(b,'/cancel')==b'CANCELLED'
    assert b'overflow' not in b'\n'.join(rows(b,'INFO 2',b'END INFO'))
    assert cmd(a,'INFOEDIT 4')==b'BOARD INFO EDIT'
    a.send(b'x'*2000+b'\r\n');a.send(('y'*996+'あいう\r\n').encode())
    assert cmd(a,'/save')==b'BOARD INFO SAVED'
    # Disabled log capture also releases old stored access rows for that board.
    assert cmd(a,'BOARDOPTIONS 3 BOARD 3 0 0 0')==b'BOARD OPTIONS SAVED'
    before=pathlib.Path(str(board)+'.boardaccess').stat().st_size
    read(c,ident);assert pathlib.Path(str(board)+'.boardaccess').stat().st_size==before
    assert cmd(a,'BOARDOPTIONS 3 BOARD 3 0 0 1')==b'BOARD OPTIONS SAVED'
    read(c,ident)
   finally:c.close();b.close();g.close()
  finally:a.close();s.close()
  # A missing INFO commit must never expose a partially saved guide.
  policy=pathlib.Path(str(board)+'.boardpolicy');original=policy.read_bytes()
  chunks=[original[i:i+4096] for i in range(8,len(original),4096)]
  end=max(i for i,row in enumerate(chunks) if row[:1]==b'E')
  policy.write_bytes(original[:8]+b''.join(chunks[:end]))
  failed=subprocess.run([binary,'0',str(board),str(users)],capture_output=True,timeout=10)
  assert failed.returncode and policy.read_bytes()==original[:8]+b''.join(chunks[:end])
  policy.write_bytes(original)
  spec=importlib.util.spec_from_file_location('retention',pathlib.Path(__file__).resolve().parents[1]/'scripts/log-retain.py')
  retention=importlib.util.module_from_spec(spec);spec.loader.exec_module(retention)
  retention.run(pathlib.Path(str(board)+'.boardaccess'),p/'retained',0)
  for suffix in ('.boardpolicy','.boardmoves','.boardaccess'):
   file=pathlib.Path(str(board)+suffix);saved=file.read_bytes();file.unlink()
   failed=subprocess.run([binary,'0',str(board),str(users)],capture_output=True,timeout=10)
   assert failed.returncode and not file.exists(),suffix
   file.write_bytes(saved)
  if os.name=='posix':
   import fault_io as resource
   import signal
   # Fail the first move journal append: no success receipt, no article relocation.
   q=p/'fault';q.mkdir()
   for f in p.iterdir():
    if f.is_file():shutil.copy2(f,q/f.name)
   def constrain():
    signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(1024,1024))
   s=Server(binary,q/'notes',members=q/'users',admins=q/'admins',options=['--user-log','-'],preexec_fn=constrain);a=Client(s.port)
   try:
    auth(a,'admin');cmd(a,'BOARD 3')
    assert cmd(a,'MOVE 1 4 CONFIRM')==b'ERR board policy storage'
   finally:a.close();s.close()
   damaged=(q/'notes.boardmoves').read_bytes();assert len(damaged)==1024
   result=subprocess.run([binary,'0',str(q/'notes'),str(q/'users')],capture_output=True,timeout=10)
   assert result.returncode and (q/'notes.boardmoves').read_bytes()==damaged
   for action in ('READ','POST'):
    q=p/('fault-log-'+action);q.mkdir()
    for f in p.iterdir():
     if f.is_file():shutil.copy2(f,q/f.name)
    log=q/'notes.boardaccess';limit=log.stat().st_size+100
    def loglimit():
     signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
    s=Server(binary,q/'notes',members=q/'users',admins=q/'admins',options=['--user-log','-'],preexec_fn=loglimit);a=Client(s.port)
    try:
     auth(a,'admin');cmd(a,'BOARD 3')
     if action=='READ':assert cmd(a,'READ 1')==b'ERR board access storage'
     else:
      assert cmd(a,'POST committed').startswith(b'EDIT');a.send(b'saved body\r\n')
      assert cmd(a,'.')==b'POST SAVED LOG FAILED 2'
    finally:a.close();s.close()
    assert log.stat().st_size==limit
    damaged=log.read_bytes();result=subprocess.run([binary,'0',str(q/'notes'),str(q/'users')],capture_output=True,timeout=10)
    assert result.returncode and log.read_bytes()==damaged
    if action=='POST':assert b'saved body' in (q/'notes').read_bytes()
 print('PASS stage60 safety: inherited passwords, ACL/SIGOP scope, username case, order, INFO boundary/UTF-8/incomplete commit, missing sidecars and disabled retention; '+('POSIX move/read/post short-write handling' if os.name=='posix' else 'POSIX fault tests not run on Windows'))

if __name__=='__main__':run(sys.argv[1])
