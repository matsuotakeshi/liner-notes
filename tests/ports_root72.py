"""Private chroot package registry setup for a release test, not a host install."""
from pathlib import Path
import subprocess,tempfile,hashlib,os,shutil,re,json
base=Path(__file__).resolve().parents[1]
assert base==Path('/home/takeshi/liner-notes-release72-20261103') and os.getuid()==0
root=base/'port-check/pkgroot';assert not root.exists();root.mkdir(mode=0o755)
protected=[Path('/etc/master.passwd'),Path('/etc/passwd'),Path('/etc/group'),Path('/var/db/pkg/local.sqlite')]
before={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
def copy(source,dest):
 dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source,dest);dest.chmod(0o755)
try:
 (root/'etc').mkdir();(root/'var/db/pkg').mkdir(parents=True)
 (root/'etc/master.passwd').write_text('root:*:0:0::0:0:Root:/root:/bin/sh\n')
 (root/'etc/group').write_text('wheel:*:0:root\n')
 subprocess.run(['/usr/sbin/pwd_mkdb','-p','-d',str(root/'etc'),str(root/'etc/master.passwd')],check=True)
 for exe in ['/bin/sh','/usr/sbin/pw','/usr/sbin/pwd_mkdb','/usr/bin/install','/usr/bin/id','/usr/bin/uname']:
  copy(exe,root/exe.lstrip('/'))
  result=subprocess.run(['/usr/bin/ldd',exe],capture_output=True,text=True)
  for lib in re.findall(r'(?:=> )?(/[^\s]+)',result.stdout):
   if Path(lib).is_file():copy(lib,root/lib.lstrip('/'))
 copy('/libexec/ld-elf.so.1',root/'libexec/ld-elf.so.1')
 (root/'dev').mkdir();(root/'dev/null').touch();(root/'tmp').mkdir()
 packages=list((base/'port-check/dependencies').rglob('*.pkg'))
 packages=[p for p in packages if not p.name.startswith('lrzsz-')]
 assert any(p.name.startswith('py312-aiohttp-') for p in packages)
 for p in packages:copy(p,root/'tmp'/p.name)
 with (base/'port-check/dependencies-install.log').open('w') as log:
  subprocess.run(['/usr/local/sbin/pkg','-c',str(root),'-o','HANDLE_RC_SCRIPTS=false','add','-M',*['/tmp/'+p.name for p in packages]],check=True,stdout=log,stderr=subprocess.STDOUT)
 print('PASS dependencies installed only inside',root)
finally:
 after={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
 assert before==after,'Host account/package database changed'
 (base/'port-check/host-protection.json').write_text(json.dumps({'unchanged':True,'files':list(before)},indent=2))
