"""Explicitly opt-in, disposable-host RPM/systemd lifecycle acceptance."""
import argparse,asyncio,hashlib,json,os
from pathlib import Path
import subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from webui.bridge import Bridge,response_data

def command(*args):return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT).strip()

async def probe(create=False):
 b=await Bridge('127.0.0.1',2222,telnet=True).open()
 try:
  await b.command(('REGISTER ' if create else 'LOGIN ')+'rpm74test');await b.command('rpm74-disposable-test-password')
  assert b.state['user'],b.state
  if create:
   await b.command('POST RPM persistence')
   async with b.lock:await b.literal('Stored before update and reboot')
   assert 'SAVED' in (await b.command('.'))['text']
  article=response_data((await b.command('READ 1'))['text'])['article']
  assert article['title']=='RPM persistence',article
  assert 'Stored before update and reboot' in article['body']
 finally:await b.close()

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('phase',choices=['seed','verify','crash']);p.add_argument('--disposable-host',action='store_true',required=True);a=p.parse_args()
 assert os.geteuid()==0
 assert command('getenforce')=='Enforcing'
 assert command('systemctl','is-active','liner-notes')=='active'
 pid=int(command('systemctl','show','liner-notes','--property=MainPID','--value'))
 assert int(Path(f'/proc/{pid}/status').read_text().split('Uid:')[1].split()[0])!=0
 if a.phase=='crash':
  command('systemctl','kill','--kill-whom=main','--signal=KILL','liner-notes')
  deadline=time.monotonic()+30
  while time.monotonic()<deadline:
   replacement=int(command('systemctl','show','liner-notes','--property=MainPID','--value'))
   if replacement and replacement!=pid:break
   time.sleep(.2)
  else:raise AssertionError('systemd did not recover after crash')
  time.sleep(1)
 asyncio.run(probe(a.phase=='seed'))
 print(json.dumps(dict(phase=a.phase,selinux='Enforcing',non_root=True,persistence=True,boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip())))

if __name__=='__main__':main()
