"""Restart/privacy boundaries, transaction truncation, group migration and editor isolation."""
import pathlib,sys,tempfile,subprocess,time,os,signal
from test_board import Server,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_ui import Menu
from test_stage58 import cmd,rows
from test_chat import until,join

def run(binary,ctl):
 binary,ctl=[str(pathlib.Path(x).resolve()) for x in (binary,ctl)]
 with tempfile.TemporaryDirectory(prefix='stage61-safe-') as d:
  p=pathlib.Path(d);b=p/'notes';m=p/'members';v=p/'vault';admins=p/'admins'
  subprocess.run([ctl,'init',str(v)],check=True,capture_output=True)
  # UTF-8 straddles vault read chunks. Terminal controls must never reach the UI.
  payload=b'A'*767+'あいう'.encode()+b'\x1b[2J\xc2\x9b31m\n'+b'Z'*7000
  source=p/'text';source.write_bytes(payload)
  subprocess.run([ctl,'import',str(v),str(source),'UTF8.TXT','text'],check=True,capture_output=True)
  s=Server(binary,b,members=m,vault=v)
  for name in ('admin','alice','bob'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close();admins.write_text('1\n');kw=dict(members=m,admins=admins,vault=v)
  s=Server(binary,b,**kw);a,c,g=[Client(s.port) for _ in range(3)]
  try:
   auth(a,'admin');auth(c,'alice');auth(g,'bob')
   assert cmd(c,'PROFILESET REALNAME Secret Name')==b'PROFILE SAVED'
   assert cmd(c,'PROFILEVIS HANDLE PRIVATE')==b'ERR profile text'
   assert cmd(c,'PROFILEVIS BIO PRIVATE')==b'PROFILE SAVED'
   assert cmd(c,'BIOEDIT')==b'DOCUMENT EDIT'
   for _ in range(4):c.send(b'x'*2046+b'\r\n')
   assert cmd(c,'.')==b'PROFILE SAVED'  # exactly 8192 bytes
   assert cmd(c,'BIOEDIT')==b'DOCUMENT EDIT';c.send(b'overflow\n')
   assert cmd(c,'.')==b'ERR profile text';assert cmd(c,'/cancel')==b'CANCELLED'
   assert cmd(c,'BUFFER')==b'DOCUMENT EDIT';c.send(b'Common Mixed Case\n');assert cmd(c,'/save')==b'PROFILE SAVED'
   assert cmd(c,'POST editor-isolation').startswith(b'EDIT')
   c.send(b'Original Mixed Case\n');c.send(b'/online\n');until(c,b'CHAT END')
   c.send(('/talk '+g.greeting.split()[1].decode()+'\n').encode());until(c,b'CHAT TALK OPEN ')
   c.send(b'PrivateMessage\n');until(c,b'CHAT PRIVATE TO ');until(g,b'CHAT PRIVATE FROM ')
   assert cmd(c,'//')==b'TALK CLOSED'
   assert cmd(c,'/buffer')==b'BUFFER IMPORTED';assert cmd(c,'/undo').startswith(b'DRAFT')
   assert cmd(c,'.').startswith(b'SAVED');assert read(c,1)[2]=='Original Mixed Case\r\n'
   join(c);c.send(b'#\n');until(c,b'END USERSTATS')
   c.send(b'\\\nLunch Mixed\n');assert c.line().endswith(b'MEMO SAVED')
   c.send(b'd\n');until(c,b'CALL DELIVERED 1');until(a,b'CHAT SYSOP CALL')
   c.send(b'//\n');until(c,b'CHAT CLOSED')
   assert cmd(a,'GROUP 2 100').startswith(b'RIGHTS');c.close()
   assert cmd(a,'GROUPMOVE 100 99 CONFIRM')==b'GROUP MOVED'
   g.close();g=Client(s.port);auth(g,'bob');c=Client(s.port);auth(c,'alice')
   assert b'GROUPS 99 ' in cmd(c,'RIGHTS')
   assert cmd(a,'FILEBOARDSET -|member|member')==b'FILEBOARD UPDATED'
   assert cmd(a,'FILEBOARDSIGOP -|2')==b'FILEBOARD UPDATED'
   assert cmd(c,'FILEOWNER 1 2')==b'FILE OWNER CHANGED 1'
   assert cmd(c,'FILEASCII 1 ON')==b'FILE ATTRIBUTE CHANGED 1'
   result=rows(c,'FILETYPE 1',b'END TEXT');body=b'\n'.join(result)
   assert 'あいう'.encode() in body and b'\x1b' not in body and b'\xc2\x9b' not in body
   offset=int(result[-1].split()[-1]);assert offset>0
   assert rows(c,'FILETYPE 1 '+str(offset),b'END TEXT')[-1]==b'END TEXT NEXT 0'
   assert cmd(a,'FILEBOARDSIGOP -|NONE')==b'FILEBOARD UPDATED'
   assert cmd(c,'FILEOWNER 1 3')==b'ERR vault denied'
   assert cmd(c,'EDITOR LINE');assert cmd(c,'PAGER SIZE 110 11')==b'PAGER SIZE SET';assert cmd(c,'PREFS SAVE').startswith(b'PREFS SAVED')
   g.close()
   for group in range(1,101):assert cmd(a,f'GROUP 3 {group}').startswith(b'RIGHTS 3 ')
   allgroups=','.join(map(str,range(1,101)))
   assert allgroups.encode() in cmd(a,'RIGHTS 3')
  finally:a.close();c.close();g.close();s.close()
  s=Server(binary,b,**kw);c,g=Client(s.port),Client(s.port)
  try:
   auth(c,'alice');auth(g,'bob')
   assert b'Secret Name' in b'\n'.join(rows(c,'PROFILEID 2',b'END PROFILE'))
   visible=b'\n'.join(rows(g,'PROFILEID 2',b'END PROFILE'));assert b'Secret Name' not in visible and b'xxx' not in visible
   assert not any(x.startswith(b'2 ') for x in rows(g,'PROFILEFIND REALNAME Secret'))
   assert b'GROUPS 99 ' in cmd(c,'RIGHTS')
   assert cmd(c,'POST restored').startswith(b'EDIT');c.send(b'A\n')  # restored line editor command
   assert cmd(c,'Persisted line mode')==b'DRAFT LINE INSERTED';c.send(b'//\n') # return from input
   assert cmd(c,'.').startswith(b'SAVED');assert read(c,2)[2]=='Persisted line mode\r\n'
   assert cmd(c,'BUFFER')==b'DOCUMENT EDIT';assert cmd(c,'/EDITOR BETA');assert cmd(c,'/save')==b'PROFILE SAVED'
  finally:c.close();g.close();s.close()
  s=Server(binary,b,**kw,menu=True,language='en');c=Menu(s.port)
  try:
   c.until();c.cmd('LOGIN bob','> ');c.cmd(PASSWORD);c.cmd('LANG EN')
   assert allgroups in c.cmd('RIGHTS')
   text=c.cmd('FILETYPE 1');assert 'あいう' in text and 'File 1' in text
   assert 'byte offset' in c.cmd('+')
  finally:c.close();s.close()
  profile=pathlib.Path(str(m)+'.profile');saved=profile.read_bytes();backup=profile.with_suffix('.held');profile.rename(backup)
  def rejects():
   r=subprocess.run([binary,'0',str(b),str(m)],capture_output=True,timeout=8)
   assert r.returncode and b'LISTEN' not in r.stdout,(r.returncode,r.stdout,r.stderr)
  rejects();assert not profile.exists();backup.rename(profile)
  profile.write_bytes(saved[:-4096]);rejects();profile.write_bytes(saved)
  if os.name=='posix':
   import fault_io as resource
   limit=len(saved)+100
   def partial_write():
    signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
   s=Server(binary,b,**kw,preexec_fn=partial_write,options=['--user-log','-']);c=Client(s.port)
   try:
    auth(c,'alice');assert cmd(c,'PROFILESET PHONE failure')==b'ERR extras storage'
    assert cmd(c,'PROFILE')==b'ERR extras storage';assert cmd(c,'PING')==b'PONG'
   finally:c.close();s.close()
   assert profile.stat().st_size==limit;rejects();profile.write_bytes(saved)
 print('PASS stage61 restart/privacy/missing and incomplete journals/group99/editor isolation/UTF8 text')

if __name__=='__main__':run(*sys.argv[1:])
