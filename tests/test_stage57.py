"""Real service integration: privacy, persistence, administrator controls and reclamation."""
import pathlib,sys,tempfile,subprocess,time,datetime
from test_runtime import Client
from test_board import Server
from test_members import auth,PASSWORD
from test_mail import send,read as mail_read
from test_ui import Menu

def cmd(c,text):
 c.send((text+'\n').encode());return c.line()
def through(c,end):
 out=[]
 while True:
  row=c.line();out.append(row)
  if row.startswith(end):return b'\n'.join(out)
def gather(c,text):
 c.send(b'GATHER\n');through(c,b'GATHER END');assert b'70 characters' in c.line()
 c.send((text+'\n//\n').encode());c.expect(b'GATHER SAVED')
def run(binary,ctl):
 binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
 with tempfile.TemporaryDirectory(prefix='stage57-') as d:
  p=pathlib.Path(d);board=p/'board';users=p/'users';admins=p/'admins';vault=p/'vault'
  s=Server(binary,board,members=users,language='en');a=Client(s.port);b=Client(s.port)
  try:auth(a,'alice',True);auth(b,'bob',True)
  finally:a.close();b.close();s.close()
  admins.write_text('1\n');subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
  file=p/'payload';file.write_bytes(b'public payload')
  subprocess.run([ctl,'import',str(vault),str(file),'public.bin','desc'],check=True,capture_output=True)
  opts=['--gather-count','2'];s=Server(binary,board,members=users,admins=admins,vault=vault,language='en',options=opts)
  a,b,g=Client(s.port),Client(s.port),Client(s.port)
  try:
   auth(a,'alice');auth(b,'bob')
   for text in ['CONNECTIONS','ANNOUNCE forbidden','DISCONNECT 1 CONFIRM','SHUTDOWN 1 forbidden','SHUTDOWN CANCEL']:
    assert cmd(b,text)==b'ERR admin required',text
   assert cmd(g,'GATHER')==b'ERR login required'
   for key in ['LOCATION','OCCUPATION','INTERESTS','WEBSITE','CONTACT']:assert cmd(b,key+' profile-'+key)==b'PROFILE SAVED'
   b.send(b'PROFILE bob\n');profile=through(b,b'END PROFILE');assert b'LOCATION profile-LOCATION' in profile and b'CONTACT profile-CONTACT' in profile
   a.send(b'PROFILES profile-INTERESTS\n');assert b'bob' in through(a,b'END NEXT')
   gather(b,'old-one');gather(b,'keep-two');gather(a,'新'*70)
   a.send(b'GATHER\n');out=through(a,b'GATHER END');assert b'old-one' not in out and out.index(b'keep-two')<out.index(('新'*70).encode());a.line()
   a.send(('新'*71+'\n').encode());assert b'70 characters' in a.line();assert cmd(a,'/CANCEL')==b'CANCELLED'
   g.send(b'SYSOPMAIL visitor\n');assert b'Compose mail' in g.line();g.send(b'private guest body\n.\n');g.expect(b'SYSOP MAIL SENT')
   assert b'30 seconds' in cmd(g,'SYSOPMAIL too-fast')
   assert b'FROM guest' in mail_read(a,1) and b'private guest body' in mail_read(a,1)
   assert mail_read(b,1)==b'MAIL ERROR unavailable'
   assert cmd(g,'MAIL READ 1')==b'ERR login required'
   send(b,'alice','dated','inbox data')
   a.send(b'MAIL NEWS 1970-01-01\n');out=through(a,b'MAIL END');assert b'visitor' in out and b'dated' in out
   a.send(b'MAIL NEWS 9999-12-31\n');assert b'MAIL ROW' not in through(a,b'MAIL END')
   assert cmd(a,'MAIL NEWS 2025-02-29')==b'ERR command'
   # Guest deletion uses the recipient as compaction actor (guest has no account).
   h=Client(s.port)
   try:
    h.send(b'SYSOPMAIL disposable\n');h.line();h.send(b'delete-private-guest\n.\n');h.expect(b'SYSOP MAIL SENT')
   finally:h.close()
   assert cmd(a,'MAIL DELETE 3').startswith(b'MAIL DELETE CONFIRM');assert cmd(a,'Y')==b'MAIL DELETED 3'
   cmd(a,'MAIL EXIT');cmd(b,'MAIL EXIT')
   assert cmd(b,'FILEDELETE 1 CONFIRM')==b'ERR vault denied'
   assert cmd(a,'FILEDELETE 1')==b'ERR vault syntax'
   answer=cmd(a,'FILEDELETE 1 CONFIRM');assert answer==b'FILE DELETED 1',answer
   assert not (vault/'00000001.bin').exists()
   answer=cmd(a,'FILEDELETE 1 CONFIRM');assert answer==b'FILE DELETED 1',answer
   assert cmd(a,'FILERESTORE 1 CONFIRM')==b'ERR no file'
   assert cmd(a,'FILE 1')==b'ERR no file'
   a.send(b'CONNECTIONS\n');out=through(a,b'END CONNECTIONS');guest=int(g.greeting.split()[1]);assert f'CONNECTION {guest} guest'.encode() in out
   assert cmd(a,f'DISCONNECT {guest} CONFIRM')==f'DISCONNECTED {guest}'.encode();assert not g.sock.recv(1)
   assert cmd(a,'SHUTDOWN 30 test-stop')==b'SHUTDOWN SCHEDULED 30'
   assert cmd(a,'SHUTDOWN CANCEL')==b'SHUTDOWN CANCELLED'
  finally:a.close();b.close();g.close();s.close()
  # Reload all added journals and compact guest mail without changing IDs/privacy.
  target=p/'mail.compact';subprocess.run([sys.executable,'scripts/notes_compact.py','mail',str(users)+'.mail',str(target)],check=True,capture_output=True)
  target.replace(str(users)+'.mail')
  s=Server(binary,board,members=users,admins=admins,vault=vault,language='en',options=opts);a=Client(s.port);b=Client(s.port)
  try:
   auth(a,'alice');auth(b,'bob');a.send(b'GATHER READ\n');out=through(a,b'GATHER END');assert b'keep-two' in out and b'old-one' not in out
   assert b'private guest body' in mail_read(a,1);assert mail_read(a,3)==b'MAIL ERROR unavailable';cmd(a,'MAIL EXIT')
   b.send(b'PROFILE bob\n');assert b'profile-WEBSITE' in through(b,b'END PROFILE')
   a.send(b'CHAT\n');through(a,b'CHAT JOIN');a.send(b'persist-public\n');through(a,b'CHAT SAY');a.send(b'//\n');a.expect(b'CHAT CLOSED')
  finally:a.close();b.close();s.close()
  s=Server(binary,board,members=users,admins=admins,language='en',options=opts);a=Client(s.port)
  try:
   auth(a,'alice');a.send(b'CHAT\n');out=through(a,b'CHAT JOIN');assert b'persist-public' in out
   a.send(b'//\n');a.expect(b'CHAT CLOSED')
   assert cmd(a,'SHUTDOWN 1 integration-stop')==b'SHUTDOWN SCHEDULED 1'
   s.proc.wait(timeout=8);assert s.proc.returncode==0
  finally:a.close();s.close()
  # Real menu notification preserves the draft and never crosses into password input.
  s=Server(binary,board,members=users,admins=admins,language='en',menu=True);a=Menu(s.port);b=Menu(s.port)
  try:
   a.until();b.until();a.cmd('LOGIN alice','> ');a.cmd(PASSWORD);b.cmd('LOGIN bob','> ');b.cmd(PASSWORD)
   a.cmd('PAGER OFF');b.cmd('PAGER OFF')
   assert 'ANNOUNCE' not in b.cmd('MENU')
   assert 'ANNOUNCE' in a.cmd('MENU')
   a.cmd('ANNOUNCE hello-everyone');out=b.cmd('PING');assert 'hello-everyone' in out
   a.cmd('SHUTDOWN 60 maintenance');assert '60 seconds' in b.cmd('PING')
   a.cmd('SHUTDOWN CANCEL');assert 'cancelled' in b.cmd('PING')
  finally:a.close();b.close();s.close()
 print('PASS: Gather limits/retention; profiles/search; private guest mail+compaction; date search; physical delete/retry/restart; admin ACL, broadcast, disconnect, scheduled stop; persistent public chat')
if __name__=='__main__':run(*sys.argv[1:])
