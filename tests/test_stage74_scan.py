"""Real BBS metadata pages across 12,000 articles, with monotonic cursors."""
import asyncio,json,subprocess,sys,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'scripts')]
from test_board import Server
from webui.bridge import Bridge
from link_bbs import Core

async def run(binary,generator,p):
 server=Server(binary,p/'notes',members=p/'members',telnet=True)
 user=await Bridge('127.0.0.1',server.port).open()
 await user.command('REGISTER alice');await user.command('scan-test-password');await user.close()
 # The shared scale fixture also creates mail addressed to member 2.
 user=await Bridge('127.0.0.1',server.port).open()
 await user.command('REGISTER bob');await user.command('scan-test-password');await user.close();server.close()
 subprocess.run([generator,str(p/'notes'),str(p/'members.mail'),'12000'],check=True)
 (p/'accounts').write_text('1 1\n');(p/'secret').write_text('scan-test-password\n')
 server=Server(binary,p/'notes',members=p/'members',telnet=True,options=['--link-accounts',str(p/'accounts')])
 core=await Core(dict(port=server.port,login='alice',password_file=str(p/'secret'))).open()
 try:
  await core.select(1);start=time.monotonic();cursor=1;pages=0;total=0
  while True:
   rows=await core.page(cursor);pages+=1
   if not rows:break
   assert rows[0]['id']==cursor
   assert all(not row['body'] for row in rows)
   total+=len(rows);cursor=rows[-1]['id']+1
  assert total==12000 and pages==376,(total,pages)
  assert (await core.snapshot(12000,True))['body'].startswith('scaled-body')
  print(json.dumps(dict(articles=total,metadata_requests=pages,seconds=time.monotonic()-start,body_omitted=True)),flush=True)
 finally:await core.close();server.close()

if __name__=='__main__':
 with tempfile.TemporaryDirectory() as tmp:asyncio.run(run(str(Path(sys.argv[1]).resolve()),str(Path(sys.argv[2]).resolve()),Path(tmp)))
