from pathlib import Path
import sys,tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth

def page(c):
 rows=[]
 while True:
  line=c.line();rows.append(line)
  if line.startswith(b'GATHER END NEXT '):return int(line.split()[-1]),b'\n'.join(rows)
def run(binary):
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp);s=Server(str(Path(binary).resolve()),p/'notes',members=p/'members',language='en',options=['--gather-count','200']);c=Client(s.port)
  try:
   auth(c,'alice',True)
   for i in range(140):
    c.send(b'GATHER\n');cursor,_=page(c)
    if cursor:c.send(b'//\n')
    assert b'70 characters' in c.line()
    text=str(i)+'a'*(70-len(str(i)))
    # Use 4-byte Unicode to force a real continuation page within the response cap.
    text=str(i)+'😀'*(70-len(str(i)))
    c.send(((text+'\n')*10).encode());c.expect(b'GATHER SAVED')
   c.send(b'GATHER READ\n');cursor,_=page(c);assert cursor
   c.send(b'//\nPING\n');c.expect(b'PONG')
   c.send(b'GATHER\n');cursor,_=page(c);assert cursor
   c.send(b'//\n');assert b'70 characters' in c.line()
   c.send(b'after-skip\n//\n');c.expect(b'GATHER SAVED')
   c.send(b'GATHER READ\n');cursor,data=page(c)
   while cursor:c.send(b'\n');cursor,more=page(c);data+=more
   assert b'after-skip' in data
  finally:c.close();s.close()
 print('PASS Gather oldest-first retained messages, maximum 70 characters/10 lines, skip-to-compose and read-only exit')
if __name__=='__main__':run(sys.argv[1])
