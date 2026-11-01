"""Level limits, admin exemption, public logs, retention, external help and menu order."""
import pathlib,sys,tempfile,time,shutil,socket,subprocess
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_stage58 import cmd,rows
from test_ui import Menu
ROOT=pathlib.Path(__file__).resolve().parents[1]

def run(binary):
 binary=str(pathlib.Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='stage61-ui-') as d:
  p=pathlib.Path(d);board=p/'notes';members=p/'members';admins=p/'admins'
  s=Server(binary,board,members=members)
  for name in ('admin','alice'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close();admins.write_text('1\n');kw=dict(members=members,admins=admins)
  s=Server(binary,board,**kw,options=['--idle-seconds','0','--level-seconds','2:3','--time-warning','1','--time-grace','1','--userlog-keep','2','--userlog-public']);a,c=Client(s.port),Client(s.port)
  try:
   auth(a,'admin');auth(c,'alice')
   assert rows(c,'USERLOG ALL')[0]==b'USERLOG'
   assert rows(c,'USERTABLE ALL')[0].startswith(b'USERTABLE')
   c.sock.settimeout(7);data=b''
   while block:=c.sock.recv(8192):data+=block
   assert '上限'.encode() in data,data
   assert cmd(a,'PING')==b'PONG'
  finally:a.close();c.close();s.close()
  helpdir=p/'help';shutil.copytree(ROOT/'help',helpdir)
  for lang in ('en','ja'):(helpdir/lang/'profile.txt').write_text('Custom profile help '+lang+'\n',encoding='utf-8')
  for lang in ('en','ja'):
   s=Server(binary,board,**kw,menu=True,language=lang,menu_dir=ROOT/'menus',options=['--help-dir',str(helpdir),'--banner',str(ROOT/'banner.txt')]);a,g=Menu(s.port),Menu(s.port)
   try:
    initial=a.until();g.until();assert 'Bulletin Board System' in initial and '/_____/_/_/' in initial
    a.cmd('LOGIN admin','> ');a.cmd(PASSWORD);a.cmd('LANG '+lang.upper())
    menu=a.cmd('MENU');heading=' [管理者用]' if lang=='ja' else ' [Administrator]';helpheading=' [案内・終了]' if lang=='ja' else ' [Help and exit]'
    assert helpheading in menu and heading in menu,(lang,menu)
    assert menu.index(helpheading)<menu.index(heading)<menu.index('------------------------------------------------------------'),menu
    assert heading not in g.cmd('MENU')
    assert 'Custom profile help '+lang in a.cmd('HELP PROFILE')
    assert 'Custom profile help '+lang in g.cmd('HELP PROFILE')
    assert 'EDITOR BETA' in a.cmd('HELP EDITOR')
    result=g.cmd('HELP ADMIN');assert 'Administrator commands' not in result and '管理者の操作' not in result
    a.cmd('PAGER SIZE 100 8');a.cmd('EDITOR LINE');a.cmd('PREFS SAVE');a.cmd('LOGOUT');time.sleep(1.05);a.cmd('LOGIN admin','> ');a.cmd(PASSWORD)
    bodyprompt='本文 > ' if lang=='ja' else 'Body > '
    editorprompt='Edit > '
    # Raw PREFS file has a versioned UI record and login loaded it; test actual editor next.
    result=a.cmd('POST saved editor',ending='> ');assert 'TEXT EDIT (' in result,result
    a.cmd('/CANCEL')
   finally:a.close();g.close();s.close()
  custom=helpdir/'en/profile.txt';valid=custom.read_bytes()
  for invalid in (b'',b'\x1b[2J',b'\xc0\x80',b'x'*16385):
   custom.write_bytes(invalid)
   proc=subprocess.run([binary,'--help-dir',str(helpdir),'0',str(board),str(members)],capture_output=True,timeout=8)
   assert proc.returncode and b'help:' in proc.stderr and b'LISTEN' not in proc.stdout
  custom.write_bytes(valid)
 print('PASS stage61 level limits/admin exemption/public USERLOG/external HELP/menu and banner')
if __name__=='__main__':run(sys.argv[1])
