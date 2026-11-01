"""Stage61 profiles, group100, shared editor buffer, vault and session settings."""
import pathlib,sys,tempfile,subprocess,time
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd,rows,stats

def run(binary,ctl):
 binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
 with tempfile.TemporaryDirectory(prefix='stage61-') as d:
  p=pathlib.Path(d);b=p/'notes';m=p/'members';v=p/'vault';admins=p/'admins'
  subprocess.run([ctl,'init',str(v)],check=True,capture_output=True)
  source=p/'test.txt';source.write_text('Hello mixed CASE\nSecond line\n',encoding='utf-8')
  subprocess.run([ctl,'import',str(v),str(source),'HELLO.TXT','guide'],check=True,capture_output=True)
  s=Server(binary,b,members=m,vault=v)
  for name in ('admin','alice','bob'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close();admins.write_text('1\n');kw=dict(members=m,admins=admins,vault=v)
  s=Server(binary,b,**kw);a,c,g=[Client(s.port) for _ in range(3)]
  try:
   auth(a,'admin');auth(c,'alice');auth(g,'bob')
   for field,value in [('MACHINE','PC-98'),('BIRTHDAY','1968-02-29'),('REALNAME','Private Name'),('PHONE','555-0100')]:
    assert cmd(c,f'profileset {field.lower()} {value}')==b'PROFILE SAVED'
   data=b'\n'.join(rows(g,'PROFILEID 2',b'END PROFILE'));assert b'Private Name' not in data and b'1968' not in data
   assert b'Private Name' in b'\n'.join(rows(a,'PROFILEID 2',b'END PROFILE'))
   assert cmd(c,'profilevis birthday monthday')==b'PROFILE SAVED'
   data=b'\n'.join(rows(g,'PROFILEID 2',b'END PROFILE'));assert b'BIRTHDAY 02-29' in data and b'1968' not in data
   assert not any(x.startswith(b'2 ') for x in rows(g,'PROFILEFIND REALNAME Private'))
   assert cmd(c,'PROFILEVIS MACHINE MEMBERS')==b'PROFILE SAVED'
   assert any(x.startswith(b'2 ') for x in rows(g,'PROFILEFIND MACHINE PC-98'))
   assert cmd(c,'PROFILESET BIRTHDAY 1900-02-29')==b'ERR profile text'
   assert cmd(c,'PROFILESET BIRTHDAY 1968X02-29')==b'ERR profile text'
   assert cmd(c,'BIOEDIT')==b'DOCUMENT EDIT'
   for i in range(20):c.send(('Line Mixed '+str(i)+' x'*30+'\n').encode())
   assert cmd(c,'/save')==b'PROFILE SAVED'
   assert b'Line Mixed 19' in b'\n'.join(rows(g,'PROFILEID 2',b'END PROFILE'))
   assert cmd(c,'BUFFER')==b'DOCUMENT EDIT';c.send(b'Shared Mixed\n');assert cmd(c,'/save')==b'PROFILE SAVED'
   assert cmd(c,'POST Buffer article').startswith(b'EDIT');assert cmd(c,'/buffer')==b'BUFFER IMPORTED'
   assert cmd(c,'.').startswith(b'SAVED');assert read(c,1)[2]=='Shared Mixed\r\n'
   assert cmd(c,'EDITOR LINE') # localized editor response
   assert cmd(c,'PAGER SIZE 100 35')==b'PAGER SIZE SET'
   assert cmd(c,'PREFS SAVE').startswith(b'PREFS SAVED')
   assert cmd(a,'GROUP 2 100').startswith(b'RIGHTS 2');c.close();c=Client(s.port);auth(c,'alice')
   assert any(x.startswith(b'2 ') for x in rows(a,'PROFILEFIND GROUP 100'))
   assert cmd(a,'BOARDSET 2|1|g100|g100|Group100|test')==b'BOARD UPDATED 2'
   assert cmd(c,'BOARD 2').startswith(b'BOARD 2');assert cmd(g,'BOARD 2')==b'ERR board unavailable'
   assert cmd(a,'FILEBOARDSET -|member|member')==b'FILEBOARD UPDATED'
   assert cmd(a,'FILEBOARDSIGOP -|2')==b'FILEBOARD UPDATED'
   assert cmd(c,'FILEOWNER 1 3')==b'FILE OWNER CHANGED 1'
   assert cmd(g,'FILEOWNER 1 2')==b'ERR vault denied'
   assert cmd(g,'FILEASCII 1 ON')==b'FILE ATTRIBUTE CHANGED 1'
   assert b'Hello mixed CASE' in b'\n'.join(rows(g,'FILETYPE 1',b'END TEXT'))
   assert any(x.startswith(b'FILEITEM 1 ') for x in rows(g,'FILESEARCH *.txt'))
   assert not any(x.startswith(b'FILEITEM ') for x in rows(g,'FILESEARCH *.lzh'))
   assert cmd(c,'FILEBOARDNAME -|Public files')==b'FILEBOARD UPDATED'
   assert b'/SAVE' in cmd(c,'FILEINFOEDIT -')
   cmd(c,'/EDITOR BETA');c.send(b'Long vault guide\n');assert cmd(c,'/save')==b'FILEBOARD UPDATED'
   info=b'\n'.join(rows(g,'FILEBOARDINFO -',b'END FILEBOARD'));assert b'Public files' in info and b'Long vault guide' in info
  finally:
   a.close();c.close();g.close();s.close()
if __name__=='__main__':run(*sys.argv[1:]);print('stage61 profiles/vault: PASS')
