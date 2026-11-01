"""Exercise the staged port in an isolated PREFIX without installing globally."""
from pathlib import Path
import subprocess,sys,os,shutil,tempfile,socket,shlex,pwd,sqlite3
from test_telnet import Client
from test_members import PASSWORD
from test_board import Server,read
from test_runtime import Client as Raw
from test_members import auth
base=Path('/home/takeshi/liner-notes-ports67-20261031');port=base/'comms/liner-notes';prefix=base/'prefix3'
stage=Path(subprocess.check_output(['make','BATCH=yes','DEFAULT_VERSIONS=python=3.12','WRKDIRPREFIX='+str(base/'prefix-v3-work'),'PREFIX='+str(prefix),'OPTIONS_UNSET=DOCS','-V','STAGEDIR'],cwd=port,text=True).strip())
source=stage/str(prefix).lstrip('/');assert source.is_dir() and source.resolve().is_relative_to((base/'prefix-v3-work').resolve())
assert not prefix.exists();shutil.copytree(source,prefix)
for sample in prefix.rglob('*.sample'):shutil.copyfile(sample,sample.with_suffix(''))
with tempfile.TemporaryDirectory(prefix='installed67-',dir=base) as tmp:
 p=Path(tmp);data=p/'data';data.mkdir();logs=p/'logs';logs.mkdir();cfg=p/'rc.conf';sock=socket.socket();sock.bind(('127.0.0.1',0));number=sock.getsockname()[1];sock.close()
 share=prefix/'share/liner-notes';(share/'sysinfo.txt').write_text('INSTALLED SITE INFORMATION\n');(share/'goodbye.txt').write_text('INSTALLED GOODBYE\n')
 settings=dict(liner_notes_enable='YES',liner_notes_program=str(prefix/'sbin/liner-notes'),liner_notes_run_user=pwd.getpwuid(os.getuid()).pw_name,liner_notes_data_dir=str(data),liner_notes_log_dir=str(logs),liner_notes_menu_dir=str(share/'menus'),liner_notes_help_dir=str(share/'help'),liner_notes_banner=str(share/'banner.txt'),liner_notes_port=str(number),liner_notes_language='en',liner_notes_pidfile=str(p/'pid'),liner_notes_child_pidfile=str(p/'child'),liner_notes_database=str(data/'liner.sqlite'),liner_notes_timezone='Asia/Tokyo',liner_notes_limits=str(prefix/'etc/liner-notes/limits.conf'),liner_notes_sysinfo=str(share/'sysinfo.txt'),liner_notes_goodbye=str(share/'goodbye.txt'))
 cfg.write_text(''.join(k+'='+shlex.quote(v)+'\n' for k,v in settings.items()))
 def service(action,ok=True):
  result=subprocess.run(['/bin/sh',str(prefix/'etc/rc.d/liner_notes'),action],env=dict(os.environ,LINER_NOTES_RC_CONF=str(cfg)),capture_output=True,text=True,timeout=40)
  if ok:assert result.returncode==0,(result.stdout,result.stderr,(logs/'daemon.log').read_text() if (logs/'daemon.log').exists() else '')
  return result
 try:
  service('start');assert 'running' in service('status').stdout
  c=Client(number);c.until();c.cmd('REGISTER portprobe',b'> ');c.cmd(PASSWORD);c.cmd('PAGER OFF')
  assert b'INSTALLED SITE INFORMATION' in c.cmd('S');c.cmd('I');assert b'Asia/Tokyo' in c.cmd('D');c.cmd('MENU')
  c.cmd('POST Packaged database',b'Body > ');c.cmd('persist from installed port',b'Body > ');assert b'Saved article #1' in c.cmd('.')
  c.cmd('O',b'> ');assert b'INSTALLED GOODBYE' in c.cmd('Y',b'INSTALLED GOODBYE');c.close()
  service('restart');c=Client(number);c.until();c.cmd('LOGIN portprobe',b'> ');c.cmd(PASSWORD);assert b'persist from installed port' in c.cmd('READ 1');c.close();service('stop')
 finally:service('stop',False)
 assert not (data/'notes.dat').exists() and not (data/'members.dat').exists()
 with sqlite3.connect(data/'liner.sqlite') as db:assert db.execute('pragma integrity_check').fetchone()==('ok',)
 archive=p/'backup.tar.gz';clean=p/'clean';admin=prefix/'bin/liner-notes-admin'
 for args in [('db-backup','--root',str(data),'--database',str(data/'liner.sqlite'),'--board','notes.dat','--members','members.dat',str(archive)),('verify',str(archive)),('db-compact',str(archive),str(clean),'--all-stores','--reclaim-withdrawn')]:
  result=subprocess.run([str(admin),*args],capture_output=True,text=True,timeout=40);assert result.returncode==0,(args,result.stdout,result.stderr)
 s=Server(str(prefix/'sbin/liner-notes'),clean/'notes.dat',members=clean/'members.dat',options=['--database',str(clean/'liner.sqlite')]);c=Raw(s.port)
 try:auth(c,'portprobe');assert read(c,1)[2]=='persist from installed port\r\n'
 finally:c.close();s.close()
 print('PASS installed PREFIX: rc.conf SQLite/timezone/limits/assets, daemon lifecycle, direct DB writes, packaged backup/verify/compact/restore')
