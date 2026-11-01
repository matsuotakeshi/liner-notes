"""Japanese/English menus, private reports and paging through real sessions."""
import pathlib,sys,tempfile,subprocess
from test_board import Server
from test_runtime import Client
from test_members import auth,PASSWORD
from test_ui import Menu

def run(binary,ctl):
 binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve())
 with tempfile.TemporaryDirectory(prefix='stage58-ui-') as name:
  p=pathlib.Path(name);data=p/'notes';users=p/'users';admins=p/'admins';vault=p/'vault'
  s=Server(binary,data,members=users)
  for name in ('alice','bob'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close();admins.write_text('1\n');subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
  for lang in ('ja','en'):
   s=Server(binary,data,members=users,admins=admins,vault=vault,menu=True,language=lang,telnet=True)
   a=b=None
   try:
    # Native menu client handles the Telnet negotiation used by runtime.
    from test_telnet import Client as Telnet
    a=Telnet(s.port);b=Telnet(s.port);a.until();b.until()
    for c,name in ((a,'alice'),(b,'bob')):
     c.cmd('LOGIN '+name,b'> ');c.cmd(PASSWORD);c.cmd('LANG '+lang);c.cmd('PAGER OFF')
    admin=a.cmd('?');ordinary=b.cmd('?');assert b'BOARDSET' in admin and b'BOARDSET' not in ordinary
    report=b.cmd('USERSTATS');assert ('ログイン' if lang=='ja' else 'logins').encode() in report
    assert ('接続履歴' if lang=='ja' else 'Connection history').encode() in b.cmd('USERLOG')
    assert ('管理者' if lang=='ja' else 'administrator').encode() in b.cmd('USERLOG ALL').lower()
    assert ('取得回数' if lang=='ja' else 'download counts').encode() in b.cmd('FILESTATS')
    assert ('ダウンロード履歴' if lang=='ja' else 'Download history').encode() in a.cmd('FILEHISTORY')
    assert ('保存' if lang=='ja' else 'saved').encode() in a.cmd('BOARDSET 2|1|all|member|Title|Description')
    assert b'Title|Description' in a.cmd('BOARDINFO 2')
    assert ('変更履歴' if lang=='ja' else 'settings history').encode() in a.cmd('BOARDLOG')
    assert ('保存' if lang=='ja' else 'saved').encode() in a.cmd('FILEBOARDSET -|member|member')
    assert b'-|member|member' in a.cmd('FILEBOARDINFO -')
    for i in range(12):a.cmd(f'BOARDSET 2|1|all|member|Title{i}|Description')
    a.cmd('PAGER ON');a.cmd('PAGER SIZE 80 5')
    a.sock.sendall(b'BOARDLOG\r\n');screen=a.until(b'OFF]> ')
    assert b'OFF]>' in screen
    # PAGER OFF flushes the rest; + retrieves the next logical history page.
    a.cmd('PAGER OFF');assert b'ERR ' not in a.cmd('+')
   finally:
    if a:a.close()
    if b:b.close()
    s.close()
 print('PASS: Japanese/English reports and HELP/menu roles; Telnet and report paging ON/OFF')
if __name__=='__main__':run(*sys.argv[1:])
