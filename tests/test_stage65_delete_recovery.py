"""Re-enter a committed cascade intent before any child has been processed."""
from pathlib import Path
import sys,tempfile,sqlite3
from test_board import Server,post
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='delete65-') as tmp:
  p=Path(tmp);admins=p/'admins'
  def start():return Server(binary,p/'notes',members=p/'members',admins=admins if admins.exists() else None)
  s=start();a=Client(s.port)
  try:auth(a,'admin',True)
  finally:a.close();s.close()
  admins.write_text('1\n');s=start();a=Client(s.port)
  try:
   auth(a,'admin')
   for spec in ('2|1|all|member|Parent|parent','3|2|all|member|Child|child','4|1|all|member|Spare|spare'):assert cmd(a,'BOARDSET '+spec).startswith(b'BOARD UPDATED')
   cmd(a,'BOARD 2');post(a,'parent','body parent');cmd(a,'BOARD 3');post(a,'child','body child')
   assert cmd(a,'BOARDDELETEALL 4 CASCADE CONFIRM')==b'BOARD TREE DELETED'
  finally:a.close();s.close()
  with sqlite3.connect(p/'notes.features.sqlite') as db:db.executemany('INSERT INTO board_deletions VALUES(?,?,?,0)',[(2,1,1),(3,1,2)])
  before=None
  for _ in range(2):
   s=start();a=Client(s.port)
   try:
    auth(a,'admin');assert cmd(a,'BOARD 2')==b'ERR board unavailable';assert cmd(a,'BOARD 3')==b'ERR board unavailable';assert post(a,'survivor','root')>=3
   finally:a.close();s.close()
   data=(p/'notes.extras').read_bytes()
   if before is not None:assert data==before
   before=data
   with sqlite3.connect(p/'notes.features.sqlite') as db:assert db.execute('SELECT count(*) FROM board_deletions WHERE done=0').fetchone()==(0,)
 print('stage65 cascade intent recovery, deepest-first deletion, repeat restart and no duplicate withdrawals passed')
if __name__=='__main__':run(sys.argv[1])
