"""Install/upgrade/reinstall/delete only inside the prepared release chroot."""
from pathlib import Path
import subprocess,hashlib,os,shutil,json,stat
base=Path(__file__).resolve().parents[1];root=base/'port-check/pkgroot'
assert base==Path('/home/takeshi/liner-notes-release72-20261103') and root.is_dir() and os.getuid()==0
protected=[Path('/etc/master.passwd'),Path('/etc/passwd'),Path('/etc/group'),Path('/var/db/pkg/local.sqlite')]
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
log=[]
def pkg(*args):
 p=subprocess.run(['/usr/local/sbin/pkg','-c',str(root),'-o','HANDLE_RC_SCRIPTS=false',*args],capture_output=True,text=True)
 log.append(p.stdout+p.stderr);assert p.returncode==0,(args,p.stdout,p.stderr)
try:
 shutil.copyfile(base/'port-check/old-0.71.pkg',root/'tmp/old.pkg')
 shutil.copyfile(base/'port-check/comms/liner-notes/work/pkg/liner-notes-0.72.pkg',root/'tmp/new.pkg')
 pkg('add','-M','/tmp/old.pkg')
 config=root/'usr/local/etc/liner-notes';share=root/'usr/local/share/liner-notes'
 (config/'web/templates').mkdir(parents=True,exist_ok=True);(config/'web/templates').chmod(0o700)
 edits={config/'liner_notes.conf':(config/'liner_notes.conf').read_bytes()+b'\n# preserved operator setting\n',
        config/'doors.conf':b'# preserve existing StarRoute and Clans entries\n',
        share/'banner.txt':b'CUSTOM BANNER\n',config/'web/web.toml':b'# custom web settings\n',
        config/'web/templates/index.html':b'<main>Operator template</main>\n',
        root/'var/db/liner-notes/keep.dat':b'KEEP DATA\n'}
 for p,data in edits.items():p.write_bytes(data)
 pkg('add','-f','-M','/tmp/new.pkg')
 assert all(p.read_bytes()==data for p,data in edits.items())
 assert (config/'web/templates').stat().st_mode&0o777==0o755
 assert (config/'web/web.toml.sample').stat().st_mode&0o777==0o640
 assert (config/'web/web.toml.sample').stat().st_gid==64965
 for name in ('liner_notes','liner_notes_web'):
  subprocess.run(['/usr/sbin/pw','-R',str(root),'usershow',name],check=True,capture_output=True)
 for name in ('/var/db/liner-notes-web','/var/db/liner-notes-web/spool'):
  p=root/name.lstrip('/');assert p.stat().st_uid==64965 and stat.S_IMODE(p.stat().st_mode)==0o700
 subprocess.run(['/usr/sbin/chroot',str(root),'/usr/local/sbin/liner-notes-web','--help'],check=True)
 assert (share/'LICENSE').is_file() and (share/'web/webui/static/xterm-LICENSE').is_file()
 pkg('add','-f','-M','/tmp/new.pkg');assert all(p.read_bytes()==data for p,data in edits.items())
 pkg('delete','-y','liner-notes');assert all(p.read_bytes()==data for p,data in edits.items())
 assert not (root/'usr/local/sbin/liner-notes').exists()
 pkg('add','-M','/tmp/new.pkg');assert all(p.read_bytes()==data for p,data in edits.items())
 print('PASS 0.71 -> 0.72 upgrade, repeat install, removal, settings/templates/doors/data preserved, Web UID/modes/imports')
finally:
 assert before=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
 (base/'port-check/lifecycle.log').write_text('\n'.join(log))
 (base/'port-check/lifecycle-protection.json').write_text(json.dumps({'unchanged':True,'files':list(before)},indent=2))
