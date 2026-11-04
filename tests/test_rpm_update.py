"""Disposable-host update checks; never run on an existing production install."""
import argparse,hashlib,json,subprocess
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument('phase',choices=['prepare','verify']);p.add_argument('--record',type=Path,required=True);p.add_argument('--disposable-host',action='store_true',required=True);a=p.parse_args()
assets=[Path('/etc/liner-notes/liner-notes.env'),Path('/usr/share/liner-notes/banner.txt'),Path('/usr/share/liner-notes/help/ja/read.txt')]
assets += [p for p in (Path('/etc/liner-notes/web/web.toml'),Path('/etc/liner-notes/web/templates/index.html'),Path('/etc/liner-notes/link.toml')) if p.exists()]
pid=lambda:subprocess.check_output(['systemctl','show','liner-notes','--property=MainPID','--value'],text=True).strip()
if a.phase=='prepare':
 if a.record.exists():raise SystemExit('Do not overwrite an existing update record')
 for path in assets:
  with path.open('a') as stream:stream.write('\n<!-- RPM74 customization preservation test -->\n' if path.suffix=='.html' else '\n# RPM74 customization preservation test\n')
 report=dict(pid=pid(),files={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in assets})
 a.record.write_text(json.dumps(report,indent=2))
else:
 report=json.loads(a.record.read_text())
 assert pid()==report['pid'],'RPM upgrade unexpectedly restarted the running daemon'
 for path,digest in report['files'].items():assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest,path
 assert Path('/var/lib/liner-notes/liner.sqlite').is_file()
 print('PASS RPM update: environment/banner/help preserved, no automatic restart, database retained')
