from pathlib import Path
import sys,tempfile,subprocess,time,sqlite3,os
from test_board import Server,read,post
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd
from test_stage63_memory import rss

def run(binary):
 binary=str(Path(binary).resolve());root=Path(__file__).resolve().parents[1]
 os.environ['LINER_TEST_START_TIMEOUT']='180'
 with tempfile.TemporaryDirectory(prefix='scale70-') as tmp:
  p=Path(tmp);database=p/'liner.sqlite';options=['--database-structured',str(database),'--max-articles','0','--max-mail','0','--max-user-posts','0']
  s=Server(binary,p/'notes',members=p/'members',options=options);c=Client(s.port)
  try:auth(c,'alice',True);c.close();c=Client(s.port);auth(c,'bob',True);baseline=rss(s.proc)
  finally:c.close();s.close()
  exe=p/'generate'
  subprocess.run(['cc','-std=c11','-O2','-D_XOPEN_SOURCE=700','-I'+str(root/'src'),'-I/usr/local/include',root/'tests/generate_structured.c',root/'src/storage_sqlite.c','-L/usr/local/lib','-lsqlite3','-lcrypto','-pthread','-o',exe],check=True)
  subprocess.run([exe,database,p/'notes',str(p/'members')+'.mail','10050'],check=True)
  start=time.monotonic();s=Server(binary,p/'notes',members=p/'members',options=options);c=Client(s.port)
  try:
   auth(c,'alice');assert read(c,10050)[2].startswith('scaled-body');assert post(c,'next','body')==10051
   loaded=rss(s.proc);assert loaded-baseline<64*1024*1024,(baseline,loaded)
   c.close();c=Client(s.port);auth(c,'bob');assert cmd(c,'MAIL GET 10050')==b'MAIL IMPORTED TO BUFFER'
   print('SCALE70 records=10050 articles+10050 mail RSS_DELTA_BYTES=',loaded-baseline,'START_AND_READ_SECONDS=',round(time.monotonic()-start,2))
  finally:c.close();s.close()
  with sqlite3.connect(str(p/'members')+'.mail.mail-index.sqlite') as d:
   assert d.execute('select count(*) from mail_flags where recipient=2 and unread=1 and deleted=0').fetchone()==(10050,)
   plan=' '.join(str(r) for r in d.execute('explain query plan SELECT count(*) FROM mail_flags WHERE recipient=2 AND unread=1 AND deleted=0'))
   assert 'mail_unread' in plan,plan
 print('PASS large structured data, bounded resident memory and indexed unread count')
if __name__=='__main__':run(sys.argv[1])
