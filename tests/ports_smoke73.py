"""Executed inside the private package chroot; uses disposable BBS data."""
from pathlib import Path
import os,pwd,sys,subprocess,tempfile
sys.path.insert(0,'/usr/local/share/liner-notes/scripts')
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth
from test_mail import send,read as read_mail
assert Path('/tmp/new.pkg').is_file() and Path('/tests/ports_lifecycle73.py').is_file()
account=pwd.getpwnam('liner_notes');os.setgroups([]);os.setgid(account.pw_gid);os.setuid(account.pw_uid)
home=Path('/var/db/liner-notes');mode=sys.argv[1]
fresh=tempfile.TemporaryDirectory(prefix='package-fresh-') if mode=='fresh' else None
if fresh:home=Path(fresh.name)
if mode=='fresh':
 subprocess.run(['/usr/local/bin/liner-notes-admin','--help'],check=True,stdout=subprocess.DEVNULL)
 subprocess.run(['/usr/local/bin/liner-notes-serial','--help'],check=True,stdout=subprocess.DEVNULL)
 subprocess.run(['/usr/local/bin/liner-notes-link','--help'],check=True,stdout=subprocess.DEVNULL)
 subprocess.run(['/usr/local/sbin/liner-notes-web','--help'],check=True,stdout=subprocess.DEVNULL)
 subprocess.run(['/usr/local/bin/liner-notes-vaultctl','init',str(home/'vault')],check=True,stdout=subprocess.DEVNULL)
 options=['--database-structured',str(home/'fresh.sqlite')]
else:options=['--database',str(home/'liner.sqlite')]
stem='fresh' if mode=='fresh' else 'notes'
s=Server('/usr/local/sbin/liner-notes',home/(stem+'.dat'),members=home/(stem+'.members'),options=options)
c=Client(s.port)
try:
 auth(c,'alice',mode in ('seed','fresh'))
 if mode in ('seed','fresh'):
  assert post(c,'Persisted article','Existing BBS data')==1
  assert send(c,'alice','Persisted mail','Existing private mail')==1
 else:
  assert read(c,1)[2]=='Existing BBS data\r\n'
  assert b'Existing private mail' in read_mail(c,1)
finally:
 c.close();s.close()
 if fresh:fresh.cleanup()
print('PASS installed unprivileged package runtime:',mode)
