import pathlib,sys,tempfile
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_ui import Menu
from test_talk import command
from test_preview import preview
from test_stage64_details import until

def run(binary):
 binary=str(pathlib.Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='ui56-') as d:
  p=pathlib.Path(d);data=p/'data';users=p/'users';admins=p/'admins';banner=p/'banner'
  s=Server(binary,data,members=users);a=Client(s.port);b=Client(s.port)
  try:auth(a,'alice',True);auth(b,'bob',True)
  finally:a.close();b.close();s.close()
  admins.write_text('1\n');banner.write_text('Custom welcome 日本語\n',encoding='utf-8')
  menus=p/'menus';menus.mkdir()
  for lang in ('ja','en'):(menus/(lang+'.txt')).write_text('CUSTOM MENU\n[ADMIN]\nPRIVATE ADMIN LINK\n[/ADMIN]\n',encoding='utf-8')
  for lang in ('ja','en'):
   s=Server(binary,data,members=users,admins=admins,menu=True,language=lang,menu_dir=menus,options=['--banner',str(banner)]);a=Menu(s.port);b=Menu(s.port)
   try:
    out=a.until();assert 'Custom welcome 日本語' in out and 'PRIVATE ADMIN LINK' not in out
    b.until();assert 'HELP ADMIN' not in b.cmd('HELP')
    denied=b.cmd('HELP ADMIN');assert 'HIDE' not in denied and 'SUSPEND' not in denied
    b.cmd('LOGIN bob','> ');b.cmd(PASSWORD);assert 'PRIVATE ADMIN LINK' not in b.cmd('MENU')
    a.cmd('LOGIN alice','> ');assert 'ADMIN' in a.cmd(PASSWORD)
    assert 'PRIVATE ADMIN LINK' in a.cmd('MENU')
    assert 'HELP ADMIN' in a.cmd('HELP');assert 'HIDE' in a.cmd('HELP ADMINBOARDS')
    index=a.cmd('HELP');assert index.index('HELP MAIL')<index.index('HELP PROFILE')
    assert '/KICK' not in b.cmd('HELP CHAT');assert 'FILEHIDE' not in b.cmd('HELP FILES')
    a.cmd('LOGOUT');assert 'HELP ADMIN' not in a.cmd('MENU')
   finally:a.close();b.close();s.close()
  # Empty and absent banner both allow normal startup.
  for empty in (True,False):
   if empty:banner.write_bytes(b'')
   else:banner.unlink()
   s=Server(binary,data,members=users,options=['--banner',str(banner)]);s.close()
  s=Server(binary,data,members=users,language='en');a=Client(s.port)
  try:
   auth(a,'alice');a.send(b'LANG en\n');a.line();command(a,'POST classic',b'EDIT');a.send(b'first\nsecond\n')
   command(a,'/E',b'Line editor');a.send(b'I2\ninserted\n//\n');a.expect(b'DRAFT LINE INSERTED')
   assert preview(a)[2]==b'first\r\ninserted\r\nsecond\r\n'
   a.send(b'D2\n');assert preview(a)[2]==b'first\r\nsecond\r\n'
   command(a,'/UNDO',b'DRAFT UNDONE');assert preview(a)[2]==b'first\r\ninserted\r\nsecond\r\n'
   a.send(b'E2\nreplacement\n');a.expect(b'DRAFT LINE REPLACED')
   a.send(b'Q2-3\n');assert preview(a)[2]==b'first\r\n> replacement\r\n> second\r\n'
   command(a,'/SAVE',b'DRAFT STORED');command(a,'/CANCEL',b'CANCELLED');command(a,'RESUME',b'DRAFT RESUMED')
   assert preview(a)[2]==b'first\r\n> replacement\r\n> second\r\n'
   command(a,'/EDITOR BETA',b'Beta editor');command(a,'//',b'SAVED 1');assert read(a,1)[2].startswith('first')
   command(a,'EDITOR LINE',b'Line editor');command(a,'MAIL SEND bob classic-mail',b'MAIL EDIT')
   a.send(b'A\nmail text\n//\n');a.expect(b'DRAFT LINE INSERTED');a.send(b'0\n');until(a,b'> ');command(a,'W',b'MAIL SENT 1')
   command(a,'MAIL 0',b'MAIL CLOSED');command(a,'POST limits',b'EDIT');a.send(b'A\n' + b'x'*1024 + b'\n//\n');a.expect(b'DRAFT LINE INSERTED')
   assert len(preview(a)[2])==1026;command(a,'/CANCEL',b'CANCELLED')
  finally:a.close();s.close()
 print('PASS: bilingual role menus/help, admin always visible, external/empty/missing banners; classic editor insert/replace/range quote/delete/undo/save/resume/article/mail')
if __name__=='__main__':run(sys.argv[1])
