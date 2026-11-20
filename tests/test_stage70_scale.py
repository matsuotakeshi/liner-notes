from pathlib import Path
import sys,tempfile,subprocess,time,sqlite3,os
from test_board import Server,read,post
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd,rows
from test_stage63_memory import rss

def run(binary,generator):
 binary=str(Path(binary).resolve());generator=str(Path(generator).resolve())
 os.environ['LINER_TEST_START_TIMEOUT']='180'
 with tempfile.TemporaryDirectory(prefix='scale70-') as tmp:
  p=Path(tmp);database=p/'liner.sqlite';options=['--database-structured',str(database),'--max-articles','0','--max-mail','0','--max-user-posts','0']
  s=Server(binary,p/'notes',members=p/'members',options=options);c=Client(s.port)
  try:auth(c,'alice',True);c.close();c=Client(s.port);auth(c,'bob',True);baseline=rss(s.proc)
  finally:c.close();s.close()
  subprocess.run([generator,database,p/'notes',str(p/'members')+'.mail','12000'],check=True)
  start=time.monotonic();s=Server(binary,p/'notes',members=p/'members',options=options);c=Client(s.port)
  try:
   auth(c,'alice');assert read(c,12000)[2].startswith('scaled-body');assert post(c,'next','body')==12001
   loaded=rss(s.proc);assert loaded-baseline<48*1024*1024,(baseline,loaded)
   assert any(b'scaled' in line for line in rows(c,'FIND ALL * scaled-body'))
   assert cmd(c,'SEEN 12000')==b'SEEN 12000'
   c.close();c=Client(s.port);auth(c,'bob');assert cmd(c,'MAIL GET 12000')==b'MAIL IMPORTED TO BUFFER'
   print('SCALE70 records=12000 articles+12000 mail RSS_DELTA_BYTES=',loaded-baseline,'START_AND_READ_SECONDS=',round(time.monotonic()-start,2))
  finally:c.close();s.close()
  with sqlite3.connect(database) as d:
   assert d.execute('PRAGMA integrity_check').fetchone()==('ok',)
   assert d.execute('PRAGMA foreign_key_check').fetchall()==[]
  s=Server(binary,p/'notes',members=p/'members',options=options);c=Client(s.port)
  try:
   auth(c,'alice');assert read(c,12001)[2]=='body\r\n'
   assert post(c,'after restart','body',12001)==12002
  finally:c.close();s.close()
  with sqlite3.connect(str(p/'members')+'.mail.mail-index.sqlite') as d:
   assert d.execute('select count(*) from mail_flags where recipient=2 and unread=1 and deleted=0').fetchone()==(12000,)
   plan=' '.join(str(r) for r in d.execute('explain query plan SELECT count(*) FROM mail_flags WHERE recipient=2 AND unread=1 AND deleted=0'))
   assert 'mail_unread' in plan,plan
 print('PASS large structured data, bounded resident memory and indexed unread count')
if __name__=='__main__':run(*sys.argv[1:])
