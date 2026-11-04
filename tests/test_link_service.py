"""Two real BBSes and two separate mutually authenticated link daemons."""
import asyncio,hashlib,json,os,socket,subprocess,sys,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT));sys.path.insert(0,str(ROOT/'scripts'))
from test_board import Server
from webui.bridge import Bridge,response_data
import link_transport as tls

def port():
 with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]

async def exercise(binary,p):
 servers=[];users=[];processes=[None,None];logs=[];configs=[];dirs=[]
 board=hashlib.sha256(b'link service test').hexdigest();ports=[port(),port()]
 async def wait_for(test,seconds=45):
  deadline=time.monotonic()+seconds
  while time.monotonic()<deadline:
   if await test():return
   for proc in processes:
    if proc and proc.poll() is not None:raise AssertionError('Link daemon exited')
   await asyncio.sleep(.25)
  raise AssertionError('Link convergence timeout')
 async def post(user,title,body,parent=0):
  await user.command(f'REPLY {parent} {title}' if parent else 'POST '+title)
  async with user.lock:await user.literal(body)
  assert 'SAVED' in (await user.command('.'))['text']
 def start(i):processes[i]=subprocess.Popen([sys.executable,str(ROOT/'scripts/link_service.py'),'serve','--config',str(configs[i])],stdout=logs[i],stderr=subprocess.STDOUT)
 def stop(i):
  if processes[i]:processes[i].terminate();processes[i].wait(timeout=40);processes[i]=None
 try:
  for i in range(2):
   d=p/str(i);d.mkdir();dirs.append(d);tls.generate(d/'identity','test '+str(i));logs.append(open(d/'service.log','wb'))
   seed=Server(binary,d/'notes',members=d/'members',telnet=True)
   b=await Bridge('127.0.0.1',seed.port).open();await b.command('REGISTER link');await b.command('link-test-password');await b.close();seed.close()
   (d/'accounts').write_text('1 1\n');(d/'secret').write_text('link-test-password\n');(d/'secret').chmod(0o600)
   server=Server(binary,d/'notes',members=d/'members',telnet=True,options=['--link-accounts',str(d/'accounts')]);servers.append(server)
   b=await Bridge('127.0.0.1',server.port).open();await b.command('REGISTER alice');await b.command('user-test-password');users.append(b)
  # A withdrawn parent predating link activation keeps live replies connected,
  # without exporting the original withdrawn body.
  await post(users[0],'old parent','private withdrawn original')
  await post(users[0],'old reply','retained reply',1)
  await users[0].command('WITHDRAW 1 CONFIRM')
  for i,d in enumerate(dirs):
   cfg=d/'link.toml';configs.append(cfg);other=dirs[1-i];q=lambda x:json.dumps(str(x))
   cfg.write_text(f'''[node]
identity = {q(d/'identity')}
database = {q(d/'link.sqlite')}
port = {ports[i]}
interval = 1
control_socket = {q(d/'control.sock')}
rotate_tls = true
[bbs]
port = {servers[i].port}
login = "link"
password_file = {q(d/'secret')}
[[boards]]
id = "{board}"
local = 1
[[origins]]
certificate = {q(other/'identity/identity.crt')}
boards = ["{board}"]
[[peers]]
certificate = {q(other/'identity/identity.crt')}
boards = ["{board}"]
host = "127.0.0.1"
port = {ports[1-i]}
''',encoding='utf8')
   subprocess.run([sys.executable,str(ROOT/'scripts/link_service.py'),'check','--config',str(cfg)],check=True,capture_output=True)
   start(i)
  async def has_reply():
   r=response_data((await users[1].command('READ 2'))['text']).get('article',{})
   return r.get('title')=='old reply' and r.get('parent')==1
  await wait_for(has_reply)
  async def withdrawn():return 'WITHDRAWN 1' in (await users[1].command('READ 1'))['text']
  await wait_for(withdrawn)
  async def control(i,action,peer=None):
   reader,writer=await asyncio.open_unix_connection(str(dirs[i]/'control.sock'))
   try:
    writer.write(json.dumps(dict(action=action,peer=peer)).encode()+b'\n');await writer.drain();return json.loads(await reader.readline())
   finally:writer.close();await writer.wait_closed()
  peer=tls.certificate_node(dirs[0]/'identity/identity.crt')[0]
  status=await control(1,'pause',peer);assert status['peers'][0]['paused']
  stop(1);start(1)
  async def control_ready():return (dirs[1]/'control.sock').exists()
  await wait_for(control_ready)
  assert (await control(1,'status'))['peers'][0]['paused']
  assert not (await control(1,'resume',peer))['peers'][0]['paused']
  assert (await control(1,'retry',peer))['node']
  assert b'private withdrawn original' not in (dirs[1]/'notes').read_bytes()
  # Peer unavailable: local accepted work persists, then converges on restart.
  stop(1)
  await post(users[0],'offline post','queued body')
  await users[0].command('RETITLE 3 new title')
  await asyncio.sleep(2);start(1)
  async def renamed():return response_data((await users[1].command('READ 3'))['text']).get('article',{}).get('title')=='new title'
  await wait_for(renamed)
  before=(dirs[1]/'notes').stat().st_size
  stop(1)
  peer=tls.certificate_node(dirs[0]/'identity/identity.crt')[0]
  result=subprocess.run([sys.executable,str(ROOT/'scripts/link_service.py'),'reset-peer','--config',str(configs[1]),peer],check=True,capture_output=True,text=True)
  assert peer not in json.loads(result.stdout)['cursors']
  start(1);await asyncio.sleep(3)
  assert (dirs[1]/'notes').stat().st_size==before
  status=subprocess.run([sys.executable,str(ROOT/'scripts/link_service.py'),'status','--config',str(configs[1])],check=True,capture_output=True,text=True)
  assert json.loads(status.stdout)['pending']==0
  # Live daemon owns its lock, so maintenance must not reset its cursor.
  result=subprocess.run([sys.executable,str(ROOT/'scripts/link_service.py'),'reset-peer','--config',str(configs[1]),peer],capture_output=True)
  assert result.returncode!=0
  print('PASS separate link daemons: real BBS/mTLS with rotated TLS keys, persistent pause/resume/retry control, withdrawn historical parent, outage, restart, cursor reset/replay, maintenance lock')
 finally:
  for i in range(2):stop(i)
  for log in logs:log.close()
  if sys.exc_info()[0]:
   for d in dirs:
    if (d/'service.log').exists():print((d/'service.log').read_text())
  for b in users:await b.close()
  for server in servers:server.close()

if __name__=='__main__':
 with tempfile.TemporaryDirectory(prefix='link-service73-') as tmp:asyncio.run(exercise(str(Path(sys.argv[1]).resolve()),Path(tmp)))
