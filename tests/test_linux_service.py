"""Exercise installed unit arguments as the service user; not a systemd boot test."""
from pathlib import Path
import os,pwd,shlex,subprocess
from test_telnet import Client
account=pwd.getpwnam('liner_notes')
for name in ('/var/lib/liner-notes','/var/log/liner-notes'):
    p=Path(name);p.mkdir(mode=0o700,exist_ok=True);os.chown(p,account.pw_uid,account.pw_gid)
line=next(x for x in Path('/etc/liner-notes/liner-notes.env').read_text().splitlines() if x.startswith('LINER_NOTES_OPTIONS='))
options=shlex.split(shlex.split(line)[0].split('=',1)[1])
proc=subprocess.Popen(['/usr/local/sbin/liner-notes',*options],cwd='/var/lib/liner-notes',
                      user=account.pw_uid,group=account.pw_gid,extra_groups=[],
                      stdout=subprocess.PIPE,stderr=subprocess.PIPE)
try:
    line=proc.stdout.readline();assert line.startswith(b'LISTEN 127.0.0.1 2222'),(line,proc.stderr.read())
    c=Client(2222)
    try:c.until();assert b'PONG' in c.cmd('PING')
    finally:c.close()
finally:
    proc.terminate();proc.wait(timeout=10)
assert proc.returncode==0,proc.stderr.read()
assert Path('/var/lib/liner-notes/liner.sqlite').is_file()
print('PASS installed Linux service arguments, unprivileged startup, Telnet menu/PING, SQLite creation and graceful SIGTERM')
