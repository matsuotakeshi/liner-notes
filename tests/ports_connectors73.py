"""Run installed Web/link/admin tools as the BBS user in the package chroot."""
from pathlib import Path
import asyncio,os,pwd,sys,subprocess,tempfile
sys.path[:0]=['/usr/local/share/liner-notes/web','/usr/local/share/liner-notes/scripts']
import aiohttp
from aiohttp import web
from webui.app import create_app
from test_board import Server,read
from test_runtime import Client
from test_members import auth
assert Path('/tmp/new.pkg').is_file()
account=pwd.getpwnam('liner_notes');os.setgroups([]);os.setgid(account.pw_gid);os.setuid(account.pw_uid)
async def http(tmp):
 bbs=Server('/usr/local/sbin/liner-notes',tmp/'web.dat',members=tmp/'web.members',telnet=True)
 runner=None
 try:
  runner=web.AppRunner(create_app({'web':{'origin':'http://127.0.0.1:18895','secure_cookies':False,'spool':str(tmp/'spool')},'bbs':{'host':'127.0.0.1','port':bbs.port,'telnet':True}}))
  await runner.setup();await web.TCPSite(runner,'127.0.0.1',18895).start()
  async with aiohttp.ClientSession(cookie_jar=aiohttp.CookieJar(unsafe=True),headers={'Origin':'http://127.0.0.1:18895'}) as c:
   async with c.post('http://127.0.0.1:18895/notes/api/session',json={}) as r:
    assert r.status==200,await r.text();session=await r.json()
   c.headers['X-CSRF-Token']=session['csrf']
   async with c.post('http://127.0.0.1:18895/notes/api/auth',json={'login':'packagetest','password':'package-test-only','register':True}) as r:
    assert r.status==200,await r.text();assert (await r.json())['state']['user']
   for path in ('/notes/','/notes/assets/reports-ui.js'):
    async with c.get('http://127.0.0.1:18895'+path) as r:assert r.status==200,await r.text()
 finally:
  if runner:await runner.cleanup()
  bbs.close()
with tempfile.TemporaryDirectory(prefix='installed73-') as tmp:
 p=Path(tmp);asyncio.run(http(p))
 env=dict(os.environ,PYTHONPATH='/usr/local/share/liner-notes/scripts')
 subprocess.run([sys.executable,'/tests/test_link_tls.py'],check=True,env=env)
 subprocess.run(['/usr/local/bin/liner-notes-admin','db-structure','--root','/var/db/liner-notes','--database','/var/db/liner-notes/liner.sqlite','--board','notes.dat','--members','notes.members',str(p/'structured')],check=True,stdout=subprocess.DEVNULL)
 s=Server('/usr/local/sbin/liner-notes',p/'structured/notes.dat',members=p/'structured/notes.members',options=['--database-structured',str(p/'structured/liner.sqlite')]);c=Client(s.port)
 try:auth(c,'alice');assert read(c,1)[2]=='Existing BBS data\r\n'
 finally:c.close();s.close()
print('PASS installed Web HTTP/auth/assets, pinned mutual TLS, admin wrapper/dbconvert and converted article login')
