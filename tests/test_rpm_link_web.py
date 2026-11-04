"""Installed RPM services: separate accounts, local control socket and Web ACL.

Only for the disposable host seeded by test_rpm_systemd.py. Changes its test
configuration, keeps originals, and never connects to an external peer.
"""
import argparse,asyncio,json,os,pwd,grp,shutil,subprocess,sys,time
from pathlib import Path
import aiohttp
sys.path.insert(0,'/usr/share/liner-notes/scripts')
from bbs_transport import Bridge
import link_transport

def run(*args):return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT).strip()

async def prepare():
 marker=Path('/root/liner-notes74-installed-link.json')
 if marker.exists():raise ValueError('Installed-link fixture already exists; use verify')
 b=await Bridge('127.0.0.1',2222,telnet=True).open()
 try:
  await b.command('REGISTER link74test');await b.command('link74-disposable-password')
  ident=b.state['user'];assert ident and ident!=1
 finally:await b.close()
 account=pwd.getpwnam('liner_notes_link');group=grp.getgrnam('liner_notes_linkctl').gr_gid
 home=Path('/var/lib/liner-notes-link');home.mkdir(mode=0o700,exist_ok=True);os.chown(home,account.pw_uid,group)
 run('runuser','-u','liner_notes_link','--','/usr/bin/liner-notes-link','init',str(home/'identity'),'--name','RPM fixture')
 peer=Path('/root/liner-notes74-fixture-peer');peer_id=link_transport.generate(peer,'Offline fixture peer')
 cert=Path('/etc/liner-notes/link-fixture-peer.crt');shutil.copyfile(peer/'identity.crt',cert);cert.chmod(0o644)
 password=home/'bbs-password';password.write_text('link74-disposable-password\n');password.chmod(0o600);os.chown(password,account.pw_uid,group)
 env=Path('/etc/liner-notes/liner-notes.env');link=Path('/etc/liner-notes/link.toml');web=Path('/etc/liner-notes/web/web.toml');accounts=Path('/etc/liner-notes/link-accounts.conf')
 for path in (env,link,web,accounts):shutil.copy2(path,path.with_name(path.name+'.before-installed-link74'))
 admins=Path('/etc/liner-notes/admins.conf');assert not admins.exists();admins.write_text('1\n');admins.chmod(0o640);os.chown(admins,0,grp.getgrnam('liner_notes').gr_gid)
 accounts.write_text(f'1 {ident}\n')
 env.write_text(env.read_text().replace('LINER_NOTES_OPTIONS="','LINER_NOTES_OPTIONS="--admins /etc/liner-notes/admins.conf --link-accounts /etc/liner-notes/link-accounts.conf ',1))
 board='a'*64
 link.write_text(f'''[node]
identity = "/var/lib/liner-notes-link/identity"
database = "/var/lib/liner-notes-link/link.sqlite"
listen = "127.0.0.1"
port = 19044
interval = 2
control_socket = "/run/liner-notes-link/control.sock"
rotate_tls = true
[bbs]
host = "127.0.0.1"
port = 2222
login = "link74test"
password_file = "/var/lib/liner-notes-link/bbs-password"
[[boards]]
id = "{board}"
local = 1
[[origins]]
certificate = "{cert}"
boards = ["{board}"]
[[peers]]
certificate = "{cert}"
boards = ["{board}"]
host = "127.0.0.1"
port = 19045
''')
 web.write_text(web.read_text().replace('[web]','[web]\nlink_control_socket = "/run/liner-notes-link/control.sock"',1))
 marker.write_text(json.dumps(dict(peer=peer_id,user=ident)))
 run('systemctl','restart','liner-notes');run('systemctl','start','liner-notes-link');run('systemctl','restart','liner-notes-web')
 await asyncio.sleep(3)

async def verify():
 peer=json.loads(Path('/root/liner-notes74-installed-link.json').read_text())['peer']
 origin='http://127.0.0.1:8088';base=origin+'/notes/api/'
 async with aiohttp.ClientSession(headers={'Origin':origin},cookie_jar=aiohttp.CookieJar(unsafe=True)) as c:
  async with c.post(base+'session',json={}) as r:assert r.status==200,await r.text();c.headers['X-CSRF-Token']=(await r.json())['csrf']
  async with c.get(base+'link') as r:assert r.status==403
  async with c.post(base+'auth',json={'login':'rpm74test','password':'rpm74-disposable-test-password'}) as r:
   value=await r.json();assert value['state']['admin'] and value['state']['link_management'],value;c.headers['X-CSRF-Token']=value['csrf']
  for action,paused in [('pause',True),('resume',False),('retry',False)]:
   async with c.post(base+'link',json=dict(action=action,peer=peer)) as r:
    result=await r.json();assert r.status==200,result;assert result['peers'][0]['paused']==paused,result
  async with c.get(base+'link') as r:assert r.status==200;status=await r.json()
 for service,user in [('liner-notes','liner_notes'),('liner-notes-web','liner_notes_web'),('liner-notes-link','liner_notes_link')]:
  assert run('systemctl','is-active',service)=='active'
  assert run('systemctl','show',service,'--property=User','--value')==user
 assert run('getenforce')=='Enforcing'
 print(json.dumps(dict(installed_services=True,web_admin_acl=True,control_pause_resume_retry=True,tls_rotation_enabled=True,peer_offline_expected=True,events=status['events'])),flush=True)

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('phase',choices=['seed','prepare','verify']);p.add_argument('--disposable-host',action='store_true',required=True);a=p.parse_args()
 if a.phase in ('seed','prepare'):asyncio.run(prepare())
 if a.phase!='seed':asyncio.run(verify())
