"""Persist all three checkpoints and replay an interrupted group migration."""
from pathlib import Path
import sys,tempfile,subprocess,sqlite3,time
from test_board import Server,post
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd

def page(c,command):
 c.send((command+'\n').encode());lines=[]
 while True:
  line=c.line();lines.append(line)
  if line.startswith(b'CHECK NEXT /'):return lines

def run(binary,ctl):
 binary,ctl=[str(Path(x).resolve()) for x in (binary,ctl)]
 with tempfile.TemporaryDirectory(prefix='stage64-persistence-') as d:
  p=Path(d);board=p/'notes';users=p/'members';admins=p/'admins';vault=p/'vault';source=p/'source'
  subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True);source.write_bytes(b'file')
  subprocess.run([ctl,'import',str(vault),str(source),'new.txt','new file'],check=True,capture_output=True)
  def start():return Server(binary,board,members=users,admins=admins if admins.exists() else None,vault=vault,language='en')
  s=start();a=Client(s.port);b=Client(s.port)
  try:
   auth(a,'admin',True);auth(b,'alice',True)
   for i in range(10):post(a,f'old-{i}','old body')
   assert cmd(a,'MAIL SEND alice old-mail').startswith(b'MAIL EDIT');a.send(b'old mail\n');assert cmd(a,'.')==b'MAIL SENT 1';cmd(a,'MAIL EXIT')
  finally:a.close();b.close();s.close()
  admins.write_text('1\n');time.sleep(1.05);s=start();a=Client(s.port);b=Client(s.port)
  try:
   auth(a,'admin');auth(b,'alice')
   first=page(b,'CHECK ARTICLES');assert any(x.startswith(b'NEWS ') for x in first)
   assert cmd(b,'CHECK DONE')==b'ERR finish CHECK NEXT first'
   while not any(x==b'END NEXT 0' for x in first):first=page(b,'CHECK NEXT')
   assert cmd(b,'CHECK DONE')==b'CHECKPOINT SAVED'
   for kind,needle in [('MAIL',b'old-mail'),('FILES',b'new.txt')]:
    lines=page(b,'CHECK '+kind);assert needle in b'\n'.join(lines),lines
    assert any(x.endswith(b'END NEXT 0') for x in lines),lines
    assert cmd(b,'CHECK DONE')==b'CHECKPOINT SAVED'
   with sqlite3.connect(str(board)+'.features.sqlite') as db:
    saved=db.execute('select kind,stamp from news where user=2 order by kind').fetchall()
   assert len(saved)==3 and all(x[1]>0 for x in saved)
   assert cmd(a,'BOARDSET 2|1|l2+g10|l2+g10|Group|guide')==b'BOARD UPDATED 2'
   assert cmd(a,'GROUP 2 10').startswith(b'RIGHTS 2')
  finally:a.close();b.close();s.close()
  # Simulate a durable move intent followed by process loss before either journal changes.
  with sqlite3.connect(str(board)+'.features.sqlite') as db:
   db.execute('insert into group_moves values(2,1,10,11,0)')
  s=start();a=Client(s.port);b=Client(s.port)
  try:
   auth(a,'admin');auth(b,'alice')
   assert b'g11' in cmd(a,'BOARDINFO 2')
   assert cmd(b,'BOARD 2').startswith(b'BOARD 2')
   assert b'11' in cmd(a,'RIGHTS 2')
   cmd(b,'BOARD 1')
   for kind in ['ARTICLES','MAIL','FILES']:
    lines=page(b,'CHECK '+kind);assert any(x.endswith(b'END NEXT 0') for x in lines),lines
    assert not any(b'old-' in x or b'new.txt' in x for x in lines),lines
    assert cmd(b,'CHECK CANCEL')==b'CHECK CANCELLED'
   with sqlite3.connect(str(board)+'.features.sqlite') as db:
    assert db.execute('select kind,stamp from news where user=2 order by kind').fetchall()==saved
    assert db.execute('select done from group_moves').fetchone()==(1,)
  finally:a.close();b.close();s.close()
 print('PASS: separate article/mail/file checkpoints, pagination completion, cancellation/restart, interrupted group intent recovery')
if __name__=='__main__':run(*sys.argv[1:])
