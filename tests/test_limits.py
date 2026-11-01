"""Boundary regression: generated >10k journals plus live mutations, reload/compact."""
import pathlib,sys,tempfile,struct,subprocess,shutil
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,mark
from test_mail import send,read as mail_read
from test_talk import command
from test_notes import page

def checksum(row):
 h=2166136261
 for byte in row[:-4]:h=((h^byte)*16777619)&0xffffffff
 struct.pack_into('<I',row,len(row)-4,h)

def run(binary):
 binary=str(pathlib.Path(binary).resolve())
 with tempfile.TemporaryDirectory(prefix='limits56-') as d:
  root=pathlib.Path(d);board=root/'board';users=root/'users'
  s=Server(binary,board,members=users);a=Client(s.port);b=Client(s.port)
  try:
   auth(a,'alice',True);auth(b,'bob',True)
   post(a,'needle','payload');send(a,'bob','needle-mail','payload')
  finally:a.close();b.close();s.close()
  # Clone valid persisted records, recomputing every ID/checksum. All belong
  # to alice, exercising >1000 cumulative author count as well as >10000 IDs.
  raw=board.read_bytes();row=bytearray(raw[8:]);out=bytearray(raw[:8])
  for i in range(1,10006):
   struct.pack_into('<II',row,0,i,1 if i>1 else 0);checksum(row);out.extend(row)
  board.write_bytes(out)
  mail=root/'users.mail';raw=mail.read_bytes();row=bytearray(raw[8:]);out=bytearray(raw[:8])
  for i in range(1,10006):
   struct.pack_into('<II',row,4,i,i);checksum(row);out.extend(row)
  mail.write_bytes(out)
  admins=root/'admins';admins.write_text('1\n')
  s=Server(binary,board,members=users,admins=admins);a=Client(s.port);b=Client(s.port)
  try:
   auth(a,'alice');auth(b,'bob');assert read(a,10001)[0]==1
   mark(a,10001);mark(a,10002,False)
   assert post(a,'beyond','more',10001)==10006
   assert send(a,'bob','beyond-mail','more')==10006
   assert b'payload' in mail_read(b,10001)
   command(a,'MAIL 0',b'MAIL CLOSED')
   _,rows,_=page(a,'SEARCHFROM 10001 needle');assert any(b'10001' in r for r in rows)
   command(a,'RETITLE 10001 changed',b'RETITLED')
   command(a,'WITHDRAW 10002 CONFIRM',b'WITHDRAW SAVED')
   command(a,'HIDE 10003',b'HIDDEN');command(a,'RESTORE 10003',b'RESTORED')
   command(a,'MAIL DELETE 10002',b'MAIL DELETE CONFIRM');command(a,'Y',b'MAIL DELETED 10002')
  finally:a.close();b.close();s.close()
  tool=pathlib.Path(__file__).resolve().parents[1]/'scripts/notes_compact.py'
  for kind,path in [('board',board),('mail',mail)]:
   new=path.with_suffix('.compact');subprocess.run([sys.executable,str(tool),kind,str(path),str(new)],check=True,capture_output=True);shutil.copyfile(new,path)
  s=Server(binary,board,members=users,options=['--max-articles','0','--max-mail','0','--max-user-posts','0']);a=Client(s.port);b=Client(s.port)
  try:
   auth(a,'alice');auth(b,'bob')
   _,rows,_=page(a,'UNREAD 0 10001');assert not any(row.startswith(b'#10001 ') for row in rows)
   assert read(a,10001)[1]=='changed';assert post(a,'after','more',10006)==10007;assert send(a,'bob','after','more')==10007
   assert mail_read(b,10002)==b'MAIL ERROR unavailable'
   assert b'payload' in mail_read(b,10001)
  finally:a.close();b.close();s.close()
  # Lowering a quota never prevents loading or reading existing data.
  for options in (['--max-articles','10000'],['--max-user-posts','1000'],['--max-mail','10000']):
   s=Server(binary,board,members=users,options=options);a=Client(s.port)
   try:
    auth(a,'alice');read(a,10007)
    if options[0]=='--max-mail':
     command(a,'MAIL SEND bob limit',b'MAIL EDIT');a.send(b'body\n.\n');assert b'MAIL ERROR' in a.line()
    else:
     command(a,'POST limit',b'EDIT');a.send(b'body\n.\n');a.expect(b'ERR board full')
   finally:a.close();s.close()
 print('PASS: >1000 per-author / >10000 article-mail IDs; reply/search/read/delete/moderation/compaction/restart; quotas and monotonic IDs')
if __name__=='__main__':run(sys.argv[1])
