from pathlib import Path
import sys,tempfile,os,time
from test_board import Server
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd,rows

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='doors65-') as tmp:
  p=Path(tmp);game=p/'game';game.write_text('#!/bin/sh\necho $$ > game.pid\nprintf "GAME_USER=%s:%s\\n" "$LINER_USER_ID" "$LINER_USER"\nIFS= read -r reply\nprintf "ANSWER=%s\\n" "$reply"\n');game.chmod(0o700)
  conf=p/'doors';conf.write_text(f'1|0|10|{game}|{p}|Test game\n2|5|10|{game}|{p}|Restricted\n3|0|1|{game}|{p}|Timed game\n')
  s=Server(binary,p/'notes',members=p/'members',options=['--doors',str(conf)],language='en');a=Client(s.port);g=Client(s.port)
  try:
   assert cmd(g,'DOOR 1')==b'ERR login required';auth(a,'alice',True)
   listing=rows(a,'GAMES',b'END GAMES');assert any(b'GAME 1 ' in x for x in listing) and not any(b'GAME 2 ' in x for x in listing),listing
   assert cmd(a,'DOOR 2')==b'ERR game unavailable'
   assert cmd(a,'DOOR 1').startswith(b'Game started')
   assert a.line()==b'GAME_USER=1:alice'
   a.send(b'hello\n');seen=[]
   while True:
    row=a.line();seen.append(row)
    if b'Back to BBS' in row:break
   assert b'ANSWER=hello' in seen,seen
   assert cmd(a,'PING')==b'PONG'
   assert cmd(a,'DOOR 1').startswith(b'Game started');assert a.line()==b'GAME_USER=1:alice'
   a.send(b'\x1d')
   while b'Back to BBS' not in a.line():pass
   assert cmd(a,'DOOR 3').startswith(b'Game started');assert a.line()==b'GAME_USER=1:alice'
   while b'Back to BBS' not in a.line():pass
   assert cmd(a,'PING')==b'PONG'
   assert cmd(a,'DOOR 1').startswith(b'Game started');assert a.line()==b'GAME_USER=1:alice';pid=int((p/'game.pid').read_text());a.close()
   time.sleep(.3)
   try:os.kill(pid,0)
   except ProcessLookupError:pass
   else:raise AssertionError('Door process survived disconnect')
   # Closing during a game must not prevent unrelated clients or server shutdown.
   assert cmd(g,'PING')==b'PONG'
  finally:a.close();g.close();s.close()
 print('stage65 native PTY door: auth, level, environment, return, escape passed')
if __name__=='__main__':run(sys.argv[1])
