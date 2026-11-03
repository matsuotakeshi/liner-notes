"""Exercise the real FreeBSD rc.subr + daemon + HTTP process in an isolated tree."""
import json
import os
from pathlib import Path
import pwd
import shlex
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
if not sys.platform.startswith('freebsd'):
    raise SystemExit('This acceptance test requires FreeBSD rc.subr and daemon')

with tempfile.TemporaryDirectory(prefix='ln-web-service-') as directory:
    p=Path(directory)
    (p/'templates').mkdir()
    (p/'templates/index.html').write_text('<main>Operator test template</main>{{components}}')
    (p/'templates/components.html').write_text('<template id="operator-component"></template>')
    account=pwd.getpwnam(os.environ.get('LN_WEB_TEST_USER',pwd.getpwuid(os.getuid()).pw_name))
    if os.getuid()==0:os.chown(p,account.pw_uid,account.pw_gid)
    sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
    config=p/'web config.toml'
    config.write_text(f'''[web]
listen="127.0.0.1"
port={port}
origin="http://127.0.0.1:{port}"
secure_cookies=false
messages={json.dumps(str(ROOT/'webui/messages'))}
templates="templates"
spool={json.dumps(str(p/'spool'))}
[bbs]
host="127.0.0.1"
port=1
''')
    wrapper=p/'web program'
    wrapper.write_text('#!/bin/sh\ncd '+shlex.quote(str(ROOT))+' || exit 1\nexec '+shlex.quote(sys.executable)+' -m webui "$@"\n');wrapper.chmod(0o755)
    rc=p/'liner_notes_web';rc.write_text((ROOT/'webui/deployment/liner_notes_web').read_text().replace('@PREFIX@','/usr/local'))
    pid=p/'supervisor.pid';log=p/'web.log';rcconf=p/'rc.conf'
    values={'enable':'YES','user':account.pw_name,'program':str(wrapper),'config':str(config),'pidfile':str(pid),'log':str(log)}
    rcconf.write_text(''.join('liner_notes_web_'+key+'='+shlex.quote(value)+'\n' for key,value in values.items()))
    env={**os.environ,'LINER_NOTES_WEB_RC_CONF':str(rcconf)}
    def service(action):
        return subprocess.run(['/bin/sh',str(rc),action],env=env,capture_output=True,text=True,timeout=20)
    def require(result,success=True):
        assert (result.returncode==0)==success,(result.returncode,result.stdout,result.stderr,log.read_text() if log.exists() else '')
    def ready():
        last=None
        for _ in range(30):
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/notes/api/messages?lang=en',timeout=1) as response:
                    assert json.load(response)['name']=='Liner notes'
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/notes/',timeout=1) as response:
                    html=response.read().decode()
                    assert 'Operator test template' in html and 'operator-component' in html and '{{components}}' not in html
                return
            except OSError as error:last=error;time.sleep(.1)
        raise AssertionError(('HTTP unavailable',last,log.read_text()))
    try:
        require(service('start'));ready();require(service('status'))
        assert (p/'spool').stat().st_uid==account.pw_uid
        assert pid.stat().st_uid==os.getuid()
        original=pid.read_text();require(service('start'),False);assert pid.read_text()==original
        require(service('restart'));ready();require(service('status'))
        require(service('stop'));require(service('status'),False)
        # Invalid configuration must not be reported as a successful start.
        config.write_text('invalid = [\n')
        require(service('start'),False);require(service('status'),False)
        assert 'unrecognized arguments: -P' not in log.read_text()
        print('PASS Web rc.d: actual rc.subr/daemon start, HTTP response, status, duplicate rejection, restart, stop, startup failure; paths with spaces; caller UID='+str(os.getuid())+', service UID='+str(account.pw_uid))
    finally:
        service('stop')
