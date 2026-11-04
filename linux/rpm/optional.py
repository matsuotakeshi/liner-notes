"""Stage Linux-specific Web/link service defaults inside the RPM buildroot."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
from webui.install import install
stage=Path(sys.argv[1]);python=sys.argv[2]
install(str(stage),'/usr',python,configdir='/etc/liner-notes/web',rc=False)
for name in ('web.toml','web.toml.sample'):
 p=stage/'etc/liner-notes/web'/name
 p.write_text(p.read_text().replace('https://srv.ysni.org','http://127.0.0.1:8088').replace('secure_cookies = true','secure_cookies = false').replace('/var/db/liner-notes-web/spool','/var/lib/liner-notes-web/spool').replace('port = 23','port = 2222'))
for name in ('liner-notes-web','liner-notes-link'):
 (stage/'usr/lib/systemd/system'/f'{name}.service').write_bytes((ROOT/'linux'/f'{name}.service').read_bytes())
for name in ('link.toml','link.toml.sample'):
 p=stage/'etc/liner-notes'/name
 text=p.read_text().replace('/var/db/liner-notes-link','/var/lib/liner-notes-link').replace('/usr/local/etc/liner-notes','/etc/liner-notes').replace('port = 23','port = 2222')
 text=text.replace('[node]','[node]\ncontrol_socket = "/run/liner-notes-link/control.sock"')
 p.write_text(text)
