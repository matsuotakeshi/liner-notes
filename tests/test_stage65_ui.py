from pathlib import Path
import sys,tempfile
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_ui import Menu

def run(binary):
 binary=str(Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='ui65-') as tmp:
  p=Path(tmp);s=Server(binary,p/'notes',members=p/'members',language='en');a=Client(s.port)
  try:
   auth(a,'alice',True)
   for i in range(8):post(a,'auto '+str(i),'article body '+str(i))
  finally:a.close();s.close()
  s=Server(binary,p/'notes',members=p/'members',language='en',menu=True);a=Menu(s.port)
  try:
   a.until();a.cmd('LOGIN alice','> ');a.cmd(PASSWORD)
   assert '[Read and search]' in a.cmd('MENUMODE BEGINNER')
   assert '[Read and search]' in a.cmd('PING')
   a.cmd('MENUMODE STANDARD');assert '[Read and search]' not in a.cmd('PING')
   assert '[Read and search]' in a.cmd('?')
   assert '[Read and search]' not in a.cmd('MENUMODE EXPERT')
   a.cmd('SCAN ASC ALL * *')
   a.send(b'F\n');text=a.until()
   while 'SCAN END' not in text and 'End of' not in text and 'article body 7' not in text:text+=a.until()
   assert 'article body 7' in text,text
   a.cmd('QUIT',ending='Goodbye')
  finally:a.close();s.close()
 print('stage65 menu quantity and continuous reader passed')
if __name__=='__main__':run(sys.argv[1])
