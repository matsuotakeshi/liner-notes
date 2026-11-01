"""Case-aware Telnet UI, preserved content/secrets, board management and admin idle."""
import pathlib,sys,tempfile,time,socket
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_stage58 import cmd
from test_telnet import Client as Telnet
ROOT=pathlib.Path(__file__).resolve().parents[1]

def run(binary):
 binary=str(pathlib.Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='stage60-ui-') as d:
  p=pathlib.Path(d);board=p/'notes';users=p/'users';admins=p/'admins'
  s=Server(binary,board,members=users)
  for name in ('admin','alice'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close();admins.write_text('1\n');kw=dict(members=users,admins=admins)
  s=Server(binary,board,**kw,options=['--idle-seconds','2']);a=Client(s.port)
  try:
   auth(a,'admin');time.sleep(3);assert cmd(a,'ping')==b'PONG'
   g=Client(s.port);g.sock.settimeout(5)
   try:assert g.line()==b'BYE idle timeout'
   finally:g.close()
   c=Client(s.port);auth(c,'alice');c.sock.settimeout(5)
   try:assert c.line()==b'BYE idle timeout'
   finally:c.close()
   assert cmd(a,'PING')==b'PONG'
  finally:a.close();s.close()
  for lang in ('en','ja'):
   s=Server(binary,board,**kw,menu=True,telnet=True,language=lang,menu_dir=ROOT/'menus',options=['--banner',str(ROOT/'banner.txt')]);a=Telnet(s.port);c=Telnet(s.port)
   try:
    banner=a.until();c.until();assert b'Bulletin Board System' in banner
    a.cmd('login admin',b'> ');a.cmd(PASSWORD)
    c.cmd('login alice',b'> ');c.cmd(PASSWORD)
    result=c.cmd('lang EN');assert b'LANG EN' in result and b'Display language: en' in result
    assert '表示言語'.encode() in c.cmd('lang JA')
    c.cmd('lang EN');a.cmd('lang EN')
    assert b'PAGER OFF' in c.cmd('pager off')
    result=c.cmd('post Mixed Title',b'Body > ');assert b'POST Mixed Title' in result and b'MIXED TITLE' not in result
    result=c.cmd('Mixed Body stays Lower',b'Body > ');assert b'Mixed Body stays Lower' in result
    result=c.cmd('/preview',b'Body > ');assert b'Mixed Body stays Lower' in result
    result=c.cmd('/cancel');assert b'cancelled' in result
    c.cmd('post Imported Title',b'Body > ');c.cmd('Original Body',b'Body > ');result=c.cmd('.')
    import re
    ident=int(re.search(rb'Saved article #(\d+)',result)[1])
    c.cmd(f'read {ident}');assert b'shared edit buffer' in c.cmd('g');c.cmd('BUFFER',b'Body > ')
    assert b'Original Body' in c.cmd('/preview',b'Body > ');c.cmd('/cancel')
    a.cmd('boardset 2|1|all|member|Child|short')
    assert b'password updated' in a.cmd('boardpass 2 none confirm')
    a.cmd('infoedit 2',b'Body > ')
    for i in range(12):a.cmd('Guide line '+str(i),b'Body > ')
    assert b'information saved' in a.cmd('/save')
    result=c.cmd('info 2');assert b'Guide line 11' in result,result
    time.sleep(1.05);a.cmd('boardpass 2',b'> ')
    secret='CaseSensitiveBoardPass'
    assert secret.encode() not in a.cmd(secret,b'> ');assert secret.encode() not in a.cmd(secret)
    assert b'board password' in c.cmd('board 2',b'> ')
    assert secret.encode() not in c.cmd(secret)
    assert b'Guide line 11' in c.cmd('INFO')
    c.cmd('pager on');c.cmd('pager size 80 8')
    result=c.cmd('info 2',b'[Enter/Q/OFF]> ');assert b'Guide line 11' not in result
    time.sleep(1.05);a.cmd('boardpass 2',b'> ');a.cmd(secret+'new',b'> ');a.cmd(secret+'new')
    assert b'Guide line 11' not in c.cmd('') # revoked pager cannot serve buffered content
    c.cmd('pager off')
    assert b'Board #2 article access' in a.cmd('boardaccess 2')
    a.cmd('boardoptions 2 MENU 0 0 0 2')
    assert b'write=no' in a.cmd('RAW',b'RAW\r\n')+a.cmd('BOARDS',b'END NEXT 0\r\n')
   finally:a.close();c.close();s.close()
 print('PASS stage60 UI: global admin idle exemption, guest/member cutoff, uppercase command echo, case-sensitive content/passwords, JA/EN, lowercase editor commands, G import and long INFO')

if __name__=='__main__':run(sys.argv[1])

