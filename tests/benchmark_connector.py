"""Repeatable loopback transfer benchmark; SHA256 checked in both directions."""
import asyncio,base64,hashlib,json,re,subprocess,sys,tempfile,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from webui.bridge import Bridge
from test_board import Server

async def run(server,size):
 b=await Bridge('127.0.0.1',server.port).open()
 try:
  await b.command('REGISTER bench');await b.command('benchmark-password')
  block=bytes(range(256))*256
  h=hashlib.sha256()
  for off in range(0,size,len(block)):h.update(block[:min(len(block),size-off)])
  digest=h.hexdigest()
  result=await b.command(f'FILESTART {size} {digest} YmVuY2guYmlu -')
  ident=int(re.search(r'UPLOAD (\d+)',result['text'])[1]);start=time.monotonic()
  for off in range(0,size,len(block)):
   data=block[:min(len(block),size-off)]
   result=await b.command(f'FILEPUTBLOCK {ident} {off} '+base64.b64encode(data).decode())
   assert f'RECEIVED {ident} {off+len(data)}' in result['text'],result
  assert 'UPLOADED' in (await b.command(f'FILEFINISH {ident}'))['text']
  upload=time.monotonic()-start;start=time.monotonic()
  ticket=(await b.command(f'FILEOPEN {ident}'))['text'].split()[1];h=hashlib.sha256()
  for off in range(0,size,len(block)):
   text=(await b.command(f'FILEFETCHBLOCK {ticket} {off}'))['text']
   match=re.search(r'DOWNLOADDATA (\d+) (\d+) (\S+)',text);assert match,text
   assert int(match[1])==off
   data=base64.b64decode(match[3],validate=True);assert len(data)==int(match[2]);h.update(data)
  assert h.hexdigest()==digest
  result=await b.command(f'FILEDONE {ticket} {digest}');assert 'ERR' not in result['text'],result
  download=time.monotonic()-start
  print(json.dumps({'bytes':size,'upload_seconds':upload,'download_seconds':download,'upload_MiB_s':size/upload/1048576,'download_MiB_s':size/download/1048576,'sha256':digest}))
 finally:await b.close()

if __name__=='__main__':
 binary,ctl=map(lambda x:str(Path(x).resolve()),sys.argv[1:3]);size=int(sys.argv[3]) if len(sys.argv)>3 else 4194304
 with tempfile.TemporaryDirectory(prefix='bench73-') as tmp:
  p=Path(tmp);subprocess.run([ctl,'init',str(p/'vault')],capture_output=True,check=True)
  s=Server(binary,p/'notes',members=p/'members',vault=p/'vault',telnet=True)
  try:asyncio.run(run(s,size))
  finally:s.close()
