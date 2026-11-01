"""Stage68: scope, read filters, disconnect persistence (real TCP)."""
from pathlib import Path
import sys,tempfile,time
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet
from test_stage58 import cmd

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp);users=p/'users';notes=p/'notes';cat=p/'catalog'
  cat.write_text('1|0|member|member|Root|root\n2|1|member|member|Child|child\n3|0|member|member|Outside|outside\n')
  for database in (False,True):
   opts=['--database',str(p/'db')] if database else []
   s=Server(binary,notes,members=users,catalog=cat,options=opts);a=Client(s.port)
   try:
    auth(a,'alice',not database)
    if not database:
     post(a,'match first','MATCH FIRST');cmd(a,'BOARD 2');post(a,'match child','MATCH CHILD');post(a,'noise','NOISE');post(a,'match last','MATCH LAST');cmd(a,'BOARD 3');post(a,'match outside','OUTSIDE');cmd(a,'BOARD 1')
    a.send(b'CHECK ARTICLES SUBTREE\n');rows=[]
    while True:
     line=a.line();rows.append(line)
     if line.startswith(b'CHECK END'):break
     if line.startswith(b'END NEXT'):break
    data=b'\n'.join(rows);assert b'match child' in data and b'match first' in data and b'outside' not in data,data
   finally:a.close();s.close()
   s=Server(binary,notes,members=users,catalog=cat,menu=True,telnet=True,language='en',options=opts);c=Telnet(s.port)
   try:
    c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD);c.cmd('LANG EN');c.cmd('PAGER OFF')
    data=c.cmd('SCAN ASC SUBTREE alice match');assert b'MATCH FIRST' in data,data
    assert b'MATCH CHILD' in c.cmd('')
    assert b'MATCH CHILD' in c.cmd('L')
    assert b'MATCH LAST' in c.cmd('')
    assert b'MATCH CHILD' in c.cmd('P')
    c.cmd('STOP');c.cmd('LANG JA');c.cmd('PAGER ON');c.close();time.sleep(.3)
    c=Telnet(s.port);c.until();c.cmd('LOGIN alice',b'> ');c.cmd(PASSWORD)
    value=c.cmd('PREFS');assert b'ja' in value and b'ON' in value,value
    c.cmd('PAGER OFF')
   finally:c.close();s.close()
 print('PASS stage68 scoped news, filtered P/L, disconnect preferences, journal and SQLite')
if __name__=='__main__':run(sys.argv[1])
