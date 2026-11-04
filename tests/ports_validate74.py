"""Finish package validation inside the guarded release chroot."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess
base=Path(__file__).resolve().parents[1];root=base/'port-check/pkgroot'
assert base==Path('/home/takeshi/liner-notes-release74-20261104') and os.getuid()==0 and root.is_dir()
protected=[Path(p) for p in ('/etc/master.passwd','/etc/passwd','/etc/group','/var/db/pkg/local.sqlite')]
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
try:
 shutil.copytree('/usr/share/zoneinfo',root/'usr/share/zoneinfo',dirs_exist_ok=True)
 for p in Path('/usr/lib/ossl-modules').glob('*.so'):
  target=root/str(p).lstrip('/');target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
 shutil.copyfile(base/'tests/ports_connectors74.py',root/'tests/ports_connectors74.py')
 with (base/'port-check/installed-connectors.log').open('w') as log:
  result=subprocess.run(['/usr/sbin/chroot',str(root),'/usr/local/bin/python3.12','/tests/ports_connectors74.py'],stdout=log,stderr=subprocess.STDOUT)
 print((base/'port-check/installed-connectors.log').read_text());result.check_returncode()
finally:
 assert before=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
 (base/'port-check/installed-protection.json').write_text(json.dumps({'unchanged':True,'files':list(before)},indent=2))
