"""Complete dependencies in the explicitly isolated release chroot."""
from pathlib import Path
import subprocess,hashlib,os,shutil,json
base=Path(__file__).resolve().parents[1];root=base/'port-check/pkgroot'
assert base==Path('/home/takeshi/liner-notes-release72-20261103') and root.is_dir() and os.getuid()==0
protected=[Path('/etc/master.passwd'),Path('/etc/passwd'),Path('/etc/group'),Path('/var/db/pkg/local.sqlite')]
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
try:
 for directory in ('/lib','/usr/lib'):
  for p in Path(directory).glob('*.so*'):
   if p.is_file():
    q=root/str(p).lstrip('/');q.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,q)
 (root/'var/run').mkdir(exist_ok=True)
 shutil.copyfile('/sbin/ldconfig',root/'tmp/ldconfig');(root/'tmp/ldconfig').chmod(0o755)
 packages=[p for p in (root/'tmp').glob('*.pkg') if not p.name.startswith('lrzsz-')]
 with (base/'port-check/dependencies-retry.log').open('w') as log:
  subprocess.run(['/usr/local/sbin/pkg','-c',str(root),'-o','RUN_SCRIPTS=false','-o','HANDLE_RC_SCRIPTS=false','add','-M',*['/tmp/'+p.name for p in packages]],check=True,stdout=log,stderr=subprocess.STDOUT)
 subprocess.run(['/usr/sbin/chroot',str(root),'/tmp/ldconfig','/lib','/usr/lib','/usr/local/lib'],check=True)
 subprocess.run(['/usr/sbin/chroot',str(root),'/usr/local/bin/python3.12','-c','import aiohttp,sqlite3; print("Isolated aiohttp",aiohttp.__version__)'],check=True)
 print('PASS isolated dependencies and Python imports')
finally:
 assert before=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
 (base/'port-check/host-protection.json').write_text(json.dumps({'unchanged':True,'files':list(before)},indent=2))
