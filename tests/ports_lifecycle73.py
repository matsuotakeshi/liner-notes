"""0.72 -> 0.73 update and fresh install in a guarded private chroot."""
from pathlib import Path
import subprocess,hashlib,os,shutil,json,stat
base=Path(__file__).resolve().parents[1];root=base/'port-check/pkgroot'
assert base==Path('/home/takeshi/liner-notes-release73-20261104') and root.is_dir() and os.getuid()==0
protected=[Path(p) for p in ('/etc/master.passwd','/etc/passwd','/etc/group','/var/db/pkg/local.sqlite')]
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected};log=[]
def pkg(*args):
 p=subprocess.run(['/usr/local/sbin/pkg','-c',str(root),'-o','HANDLE_RC_SCRIPTS=false',*args],capture_output=True,text=True)
 log.append(p.stdout+p.stderr);assert p.returncode==0,(args,p.stdout,p.stderr)
def smoke(mode):subprocess.run(['/usr/sbin/chroot',str(root),'/usr/local/bin/python3.12','/tests/ports_smoke73.py',mode],check=True)
def hashes(directory):return {str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in directory.rglob('*') if p.is_file()}
try:
 shutil.copyfile(base/'tests/ports_smoke73.py',root/'tests/ports_smoke73.py')
 shutil.copyfile('/home/takeshi/liner-notes-release72-20261103/port-check/comms/liner-notes/work/pkg/liner-notes-0.72.pkg',root/'tmp/old.pkg')
 shutil.copyfile(base/'port-check/comms/liner-notes/work/pkg/liner-notes-0.73.pkg',root/'tmp/new.pkg')
 pkg('add','-f','-M','/tmp/new.pkg');smoke('fresh');pkg('delete','-y','liner-notes')
 pkg('add','-M','/tmp/old.pkg');smoke('seed')
 config=root/'usr/local/etc/liner-notes';share=root/'usr/local/share/liner-notes';home=root/'var/db/liner-notes'
 edits={config/'liner_notes.conf':(config/'liner_notes.conf').read_bytes()+b'\n# preserved operator setting\n',
  config/'doors.conf':b'1|0|3600|/existing/starroute|/existing/universe|StarRoute Traders\n2|0|3600|/existing/door_clans.sh|/usr/local/clans|The Clans|dorinfo1:1\n',
  share/'banner.txt':b'CUSTOM BANNER\n',config/'web/web.toml':b'# custom web settings\n',
  config/'web/templates/index.html':b'<main>Operator template</main>\n',home/'keep.dat':b'KEEP DATA\n'}
 for p,data in edits.items():p.write_bytes(data)
 saved=hashes(home)
 pkg('add','-f','-M','/tmp/new.pkg')
 assert hashes(home)==saved and all(p.read_bytes()==data for p,data in edits.items())
 assert stat.S_IMODE((config/'web/templates').stat().st_mode)==0o755
 assert stat.S_IMODE((config/'web/web.toml.sample').stat().st_mode)==0o640
 assert (config/'web/web.toml.sample').stat().st_gid==64965
 assert (share/'web/webui/static/reports-ui.js').is_file()
 assert (config/'link.toml.sample').is_file() and (root/'usr/local/bin/liner-notes-dbconvert').is_file()
 smoke('verify');saved=hashes(home)
 pkg('add','-f','-M','/tmp/new.pkg');assert hashes(home)==saved
 pkg('delete','-y','liner-notes');assert hashes(home)==saved
 assert all(p.read_bytes()==data for p,data in edits.items())
 assert not (root/'usr/local/sbin/liner-notes').exists()
 pkg('add','-M','/tmp/new.pkg');assert hashes(home)==saved;smoke('verify')
 pkg('check','-d','liner-notes')
 print('PASS 0.72 to 0.73 update/reinstall/delete: data hashes, account login, article/mail, custom config/banner/templates, StarRoute/Clans entries, new tools, structured fresh runtime')
finally:
 assert before=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
 (base/'port-check/lifecycle.log').write_text('\n'.join(log))
 (base/'port-check/lifecycle-protection.json').write_text(json.dumps({'unchanged':True,'files':list(before)},indent=2))
