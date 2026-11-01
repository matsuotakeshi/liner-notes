"""Permission, shared-buffer, scoped vault and timezone regression checks."""
from pathlib import Path
import sys,tempfile,subprocess
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd,rows

def run(binary,vaultctl):
 binary,vaultctl=map(lambda p:str(Path(p).resolve()),(binary,vaultctl))
 with tempfile.TemporaryDirectory(prefix='stage67-') as tmp:
  p=Path(tmp);admins=p/'admins';admins.write_text('1\n')
  subprocess.run([vaultctl,'init',str(p/'vault')],check=True,capture_output=True)
  s=Server(binary,p/'notes',members=p/'members',vault=p/'vault')
  for name in ('admin','sigop','alice','bob'):
   c=Client(s.port);auth(c,name,True);c.close()
  s.close()
  s=Server(binary,p/'notes',members=p/'members',vault=p/'vault',admins=admins)
  clients=[Client(s.port) for _ in range(4)]
  try:
   for c,name in zip(clients,('admin','sigop','alice','bob')):auth(c,name)
   a,g,u,b=clients
   for spec in ('2|1|member|member|Forum|info','3|2|member|member|Child|info','4|1|member|member|Other|info'):
    assert cmd(a,'BOARDSET '+spec).startswith(b'BOARD UPDATED')
   assert cmd(a,'SIGOP 2 2').startswith(b'SIGOP SET')
   g.close();g=Client(s.port);auth(g,'sigop');clients[1]=g
   for c in clients:cmd(c,'BOARD 3')
   ident=post(u,'withdraw by inherited sigop','private reclaim marker 67')
   assert cmd(b,f'WITHDRAW {ident} CONFIRM')==b'ERR article owner'
   assert cmd(g,f'WITHDRAW {ident} CONFIRM')==f'WITHDRAW SAVED {ident}'.encode()
   log=rows(a,f'AUDIT ARTICLE {ident}')
   assert any(b'WITHDRAW' in x or b'withdraw' in x for x in log),log
   cmd(u,'BOARD 4');other=post(u,'outside scope','remain visible')
   cmd(g,'BOARD 4');assert cmd(g,f'WITHDRAW {other} CONFIRM')==b'ERR article owner'
   cmd(a,'BOARD 4');assert cmd(a,f'WITHDRAW {other} CONFIRM')==f'WITHDRAW SAVED {other}'.encode()
   cmd(u,'BOARD 3');source=post(u,'copy','shared source')
   assert cmd(a,'BOARDSET 3|2|member|none|Child|info').startswith(b'BOARD UPDATED')
   assert cmd(u,f'GET {source}')==b'ARTICLE IMPORTED TO BUFFER'
   assert cmd(u,'PING')==b'PONG' # GET does not enter a posting editor.
   assert cmd(u,'BUFFER')==b'DOCUMENT EDIT'
   cmd(u,'/CANCEL')
   for path in ('-','games','games/action','other'):
    result=cmd(a,f'FILEBOARDSET {path}|member|member');assert result.startswith(b'FILEBOARD UPDATED'),result
   assert cmd(u,'FILEBOARD games').startswith(b'FILEBOARDSETTING games|')
   assert cmd(u,'FILESEARCH games|*.zip').startswith(b'FILESEARCH games|')
   while not u.line().startswith(b'END NEXT '):pass
   assert cmd(u,'FILENEWS games|2000-01-01').startswith(b'FILENEWS games|')
   while not u.line().startswith(b'END NEXT '):pass
  finally:
   for c in clients:c.close()
   s.close()
 # Calendar boundary and DST conversion, no modification of recorded UTC values.
 source=Path(__file__).resolve().parents[1]/'src'
 with tempfile.TemporaryDirectory(prefix='date67-') as tmp:
  p=Path(tmp);code=p/'date.c';exe=p/'date'
  code.write_text('#define _POSIX_C_SOURCE 200809L\n#include "date_filter.h"\n#include <stdio.h>\nint main(int n,char **v){uint64_t t;if(n!=3)return 2;setenv("TZ",v[1],1);tzset();if(!date_filter(v[2],&t))return 1;printf("%llu\\n",(unsigned long long)t);return 0;}\n')
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I'+str(source),str(code),'-o',str(exe)],check=True)
  def stamp(zone,date):return int(subprocess.check_output([exe,zone,date]))
  assert stamp('UTC','2026-10-31')-stamp('Asia/Tokyo','2026-10-31')==9*3600
  assert stamp('America/New_York','2026-03-09')-stamp('America/New_York','2026-03-08')==23*3600
  assert subprocess.run([exe,'UTC','2026-02-30'],capture_output=True).returncode==1
 print('PASS stage67 permission, shared buffer, file scope, timezone/DST')
if __name__=='__main__':run(*sys.argv[1:])
