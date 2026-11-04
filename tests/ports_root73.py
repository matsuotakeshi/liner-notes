"""Build a fresh, isolated chroot for 0.73 package lifecycle tests only."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,time
base=Path(__file__).resolve().parents[1]
assert base==Path('/home/takeshi/liner-notes-release73-20261104') and os.getuid()==0
root=base/'port-check/pkgroot'
if root.exists():
 assert root.is_dir() and not root.is_symlink() and root.resolve().parent==base/'port-check'
 preserved=root.with_name('pkgroot-preserved-'+str(time.time_ns()));assert not preserved.exists()
 root.rename(preserved)
protected=[Path(p) for p in ('/etc/master.passwd','/etc/passwd','/etc/group','/var/db/pkg/local.sqlite')]
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
root.mkdir(mode=0o755)
def copy(source,target):
 target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,target);target.chmod(0o755)
try:
 for directory in ('etc','var/db/pkg','var/run','dev','tmp','root'):(root/directory).mkdir(parents=True,exist_ok=True)
 (root/'tmp').chmod(0o1777);(root/'dev/null').touch();(root/'dev/null').chmod(0o666)
 (root/'etc/master.passwd').write_text('root:*:0:0::0:0:Root:/root:/bin/sh\n')
 (root/'etc/group').write_text('wheel:*:0:root\n')
 subprocess.run(['/usr/sbin/pwd_mkdb','-p','-d',str(root/'etc'),str(root/'etc/master.passwd')],check=True)
 for name in ('/bin/sh','/bin/mkdir','/bin/chmod','/bin/ln','/usr/sbin/pw','/usr/sbin/pwd_mkdb','/usr/bin/install','/usr/bin/id','/usr/bin/uname','/sbin/ldconfig','/libexec/ld-elf.so.1'):
  copy(name,root/name.lstrip('/'))
 for directory in ('/lib','/usr/lib'):
  for p in Path(directory).glob('*.so*'):
   if p.is_file():copy(p,root/str(p).lstrip('/'))
 shutil.copytree('/usr/share/zoneinfo',root/'usr/share/zoneinfo')
 for p in Path('/usr/lib/ossl-modules').glob('*.so'):copy(p,root/str(p).lstrip('/'))
 packages={}
 for directory in (Path('/home/takeshi/liner-notes-release72-20261103/port-check/dependencies'),Path('/home/takeshi/liner-notes-stage73-link-20261104/deps/pkg')):
  for p in directory.rglob('*.pkg'):
   raw=subprocess.check_output(['tar','-xOf',str(p),'+COMPACT_MANIFEST']);manifest=json.loads(raw)
   if manifest['name']=='lrzsz':continue
   packages[manifest['name']]=p
 assert {'python312','py312-aiohttp','py312-cryptography','py312-sqlite3','sqlite3'}<=packages.keys()
 for name,p in packages.items():copy(p,root/'tmp'/(name+'.pkg'))
 with (base/'port-check/dependencies-install.log').open('w') as log:
  subprocess.run(['/usr/local/sbin/pkg','-c',str(root),'-o','RUN_SCRIPTS=false','-o','HANDLE_RC_SCRIPTS=false','add','-M',*['/tmp/'+n+'.pkg' for n in packages]],check=True,stdout=log,stderr=subprocess.STDOUT)
 subprocess.run(['/usr/sbin/chroot',str(root),'/sbin/ldconfig','/lib','/usr/lib','/usr/local/lib'],check=True)
 subprocess.run(['/usr/sbin/chroot',str(root),'/usr/local/bin/python3.12','-c','import aiohttp,sqlite3,cryptography; print(aiohttp.__version__,cryptography.__version__)'],check=True)
 shutil.copytree(base/'tests',root/'tests',ignore=shutil.ignore_patterns('__pycache__'))
 print('PASS fresh private package root; Python, SQLite, Web and link dependencies import')
finally:
 assert before=={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
 (base/'port-check/host-protection.json').write_text(json.dumps({'unchanged':True,'files':list(before)},indent=2))
