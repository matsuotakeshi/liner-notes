"""Isolated real Apache HTTPS -> Web connector -> BBS concurrent transfer test.

Creates synthetic data, checks TLS/size/SHA256, and records responsiveness.
Run on a disposable Linux test host with httpd/mod_ssl installed, as root.
"""
import argparse,asyncio,base64,hashlib,json,os,socket,ssl,subprocess,sys,time
from pathlib import Path
import aiohttp
from aiohttp import web
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from test_board import Server
from webui.app import create_app
from webui.bridge import Bridge

def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]

async def exercise(a):
 root=a.output.absolute();root.mkdir(mode=0o755)
 fixture=root/'fixture.bin';block=bytes(range(256))*4096
 with fixture.open('wb') as f:
  remaining=a.bytes
  while remaining:f.write(block[:min(remaining,len(block))]);remaining-=min(remaining,len(block))
 with fixture.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
 subprocess.run([a.vaultctl,'init',str(root/'vault')],check=True,capture_output=True)
 (root/'vault/limits.conf').write_text('max_files=2000\nmax_events=10000\nmax_bytes=17179869184\n')
 server=Server(a.runtime,root/'notes',members=root/'members',vault=root/'vault',telnet=True,language='en')
 backend=port();front=port();origin=f'https://127.0.0.1:{front}';base=origin+'/notes/api/'
 cert=root/'cert.pem';key=root/'key.pem'
 subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-addext','subjectAltName=IP:127.0.0.1','-keyout',str(key),'-out',str(cert)],check=True,capture_output=True)
 config=root/'httpd.conf'
 modules=['mpm_event','authz_core','authz_host','unixd','proxy','proxy_http','ssl','socache_shmcb','log_config']
 config.write_text('\n'.join(['ServerRoot "/etc/httpd"',f'Listen 127.0.0.1:{front}','ServerName localhost',f'PidFile "{root}/httpd.pid"','User apache','Group apache',*[f'LoadModule {m}_module /usr/lib64/httpd/modules/mod_{m}.so' for m in modules],f'ErrorLog "{root}/httpd-error.log"','LogLevel warn',f'<VirtualHost 127.0.0.1:{front}>','SSLEngine on',f'SSLCertificateFile "{cert}"',f'SSLCertificateKeyFile "{key}"','ProxyRequests Off','ProxyPreserveHost On',f'ProxyPass /notes/ http://127.0.0.1:{backend}/notes/ timeout=900',f'ProxyPassReverse /notes/ http://127.0.0.1:{backend}/notes/','<Location /notes/>','Require all granted','LimitRequestBody 1073741824','</Location>','</VirtualHost>'])+'\n')
 app=create_app({'web':{'origin':origin,'listen':'127.0.0.1','secure_cookies':True,'spool':str(root/'spool')},'bbs':{'host':'127.0.0.1','port':server.port,'telnet':True,'transfer_timeout':300}})
 runner=web.AppRunner(app);await runner.setup();await web.TCPSite(runner,'127.0.0.1',backend).start()
 apache=subprocess.Popen(['/usr/sbin/httpd','-f',str(config),'-DFOREGROUND'],stdout=subprocess.DEVNULL,stderr=open(root/'httpd-start.log','wb'))
 clients=[];probe=None;monitor=None;latencies=[];stop=asyncio.Event()
 try:
  ctx=ssl.create_default_context(cafile=str(cert))
  for _ in range(100):
   if apache.poll() is not None:raise AssertionError((root/'httpd-start.log').read_text()+(root/'httpd-error.log').read_text())
   try:
    reader,writer=await asyncio.open_connection('127.0.0.1',front,ssl=ctx);writer.close();await writer.wait_closed();break
   except (OSError,ssl.SSLError):await asyncio.sleep(.1)
  else:raise AssertionError('Apache HTTPS startup timeout')
  for i in range(a.clients):
   client=aiohttp.ClientSession(headers={'Origin':origin},cookie_jar=aiohttp.CookieJar(unsafe=True),connector=aiohttp.TCPConnector(ssl=ctx),timeout=aiohttp.ClientTimeout(total=1800));clients.append(client)
   async with client.post(base+'session',json={}) as r:assert r.status==200,await r.text();client.headers['X-CSRF-Token']=(await r.json())['csrf']
   async with client.post(base+'auth',json={'login':'load'+str(i),'password':'https-load-test-only','register':True}) as r:
    value=await r.json();assert r.status==200 and value['state']['user'],value;client.headers['X-CSRF-Token']=value['csrf']
  probe=await Bridge('127.0.0.1',server.port,telnet=True).open()
  async def monitoring():
   while not stop.is_set():
    t=time.monotonic();assert 'PONG' in (await probe.command('PING'))['text'];latencies.append(time.monotonic()-t);await asyncio.sleep(.25)
  monitor=asyncio.create_task(monitoring())
  encode=lambda s:base64.b64encode(s.encode()).decode()
  async def upload(i,c):
   t=time.monotonic()
   with fixture.open('rb') as f:
    async with c.post(base+'upload',data=f,headers={'X-File-Name':encode(f'load-{i}.bin'),'X-File-Description':encode('Synthetic HTTPS load test'),'X-Upload-Async':'1'}) as r:
     value=await r.json();assert r.status==202,value;token=value['transfer']
   deadline=time.monotonic()+1800
   while time.monotonic()<deadline:
    async with c.get(base+'transfer/'+token) as r:assert r.status==200;job=await r.json()
    if job['done']:break
    await asyncio.sleep(.25)
   else:raise AssertionError('Upload completion timeout')
   result=job['result'];assert job['http_status']==200 and result['sha256']==digest and result['size']==a.bytes,job
   return dict(id=result['id'],seconds=time.monotonic()-t)
  started=time.monotonic();uploads=await asyncio.gather(*(upload(i,c) for i,c in enumerate(clients)));upload_time=time.monotonic()-started
  async def download(c,ident):
   t=time.monotonic();h=hashlib.sha256();size=0
   async with c.post(base+'download',json={'id':ident}) as r:
    assert r.status==200,await r.text();assert r.content_length==a.bytes
    async for chunk in r.content.iter_chunked(65536):h.update(chunk);size+=len(chunk)
   assert size==a.bytes and h.hexdigest()==digest
   return time.monotonic()-t
  started=time.monotonic();downloads=await asyncio.gather(*(download(c,u['id']) for c,u in zip(clients,uploads)));download_time=time.monotonic()-started
  stop.set();await monitor
  report=dict(bytes_per_file=a.bytes,clients=a.clients,sha256=digest,verified=True,tls_verified=True,proxy='Apache httpd',synthetic=True,upload_seconds=upload_time,download_seconds=download_time,upload_mib_s=a.bytes*a.clients/1048576/upload_time,download_mib_s=a.bytes*a.clients/1048576/download_time,uploads=uploads,downloads=downloads,ping_samples=len(latencies),ping_max_seconds=max(latencies),ping_p95_seconds=sorted(latencies)[int(len(latencies)*.95)])
  (root/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report),flush=True)
 finally:
  stop.set()
  if monitor:monitor.cancel();await asyncio.gather(monitor,return_exceptions=True)
  if probe:await probe.close()
  for client in clients:await client.close()
  apache.terminate();apache.wait(timeout=20);await runner.cleanup();server.close()

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runtime',required=True);p.add_argument('--vaultctl',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--bytes',type=int,default=159809104);p.add_argument('--clients',type=int,default=4);a=p.parse_args()
 assert 1<=a.clients<=16 and 1<=a.bytes<=1073741824
 a.runtime=str(Path(a.runtime).resolve());a.vaultctl=str(Path(a.vaultctl).resolve());asyncio.run(exercise(a))
