"""Config-preserving staged installation. No accounts/services are changed."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from webui.install import install
from webui.presentation import Presentation

with tempfile.TemporaryDirectory(prefix='ln-web-install-') as d:
    root=Path(d);previous=os.umask(0o077)
    try:install(d)
    finally:os.umask(previous)
    etc=root/'usr/local/etc/liner-notes/web';config=etc/'web.toml';messages=etc/'messages/ja.json'
    config.write_text('# operator configuration\n');messages.write_text('{"name":"Operator site","greetings":{"0":"night","12":"day"}}')
    template=etc/'templates/index.html';template.write_text('<main>Operator layout</main>{{components}}')
    assert template.stat().st_mode&0o777==0o644
    assert config.stat().st_mode&0o777==0o640
    assert (etc/'templates').stat().st_mode&0o777==0o755
    (etc/'templates').chmod(0o700) # repair an installation from an older version
    install(d)
    assert (etc/'templates').stat().st_mode&0o777==0o755
    assert config.stat().st_mode&0o777==0o640
    assert config.read_text()=='# operator configuration\n'
    assert json.loads(messages.read_text())['name']=='Operator site'
    assert template.read_text()=='<main>Operator layout</main>{{components}}'
    assert (etc/'templates/index.html.sample').is_file() and (etc/'templates/components.html.sample').is_file()
    assert (etc/'web.toml.sample').is_file() and (etc/'messages/ja.json.sample').is_file()
    assert Presentation(etc/'messages','Asia/Tokyo').read('ja')['name']=='Operator site'
    messages.write_text('{"name":"Changed without restart"}')
    assert Presentation(etc/'messages','Asia/Tokyo').read('ja')['name']=='Changed without restart'
    subprocess.run(['/bin/sh','-n',str(root/'usr/local/etc/rc.d/liner_notes_web')],check=True)
    target=root/'usr/local/sbin/liner-notes-web';target.unlink();target.symlink_to(root/'outside')
    try:install(d)
    except ValueError:pass
    else:raise AssertionError('Symlink target accepted')
with tempfile.TemporaryDirectory(prefix='ln-web-pkg-stage-') as d:
    install(d,samples_only=True)
    etc=Path(d)/'usr/local/etc/liner-notes/web'
    for relative in ('web.toml','messages/ja.json','messages/en.json','templates/index.html','templates/components.html'):
        assert not (etc/relative).exists(),relative
        assert (etc/(relative+'.sample')).is_file(),relative
    assert (etc/'web.toml.sample').stat().st_mode&0o777==0o640
print('PASS Web installer: config preservation, samples-only pkg staging, permissions, rc syntax, symlink refusal')
