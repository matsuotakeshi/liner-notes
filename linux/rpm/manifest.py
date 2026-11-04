"""Generate an explicit RPM manifest from the private buildroot."""
from pathlib import Path
import sys

root = Path(sys.argv[1]).resolve()
package = sys.argv[2] if len(sys.argv)>2 else 'core'
owned = ('/etc/liner-notes', '/usr/share/liner-notes', '/usr/share/doc/liner-notes')
for p in sorted(root.rglob('*')):
    path = '/' + p.relative_to(root).as_posix()
    role='web' if path.startswith(('/etc/liner-notes/web','/usr/share/liner-notes/web/')) or path in ('/usr/share/liner-notes/web','/usr/sbin/liner-notes-web','/usr/lib/systemd/system/liner-notes-web.service') else 'link' if path in ('/usr/bin/liner-notes-link','/usr/lib/systemd/system/liner-notes-link.service') else 'core'
    if role!=package:continue
    if p.is_symlink():
        raise SystemExit('Unexpected buildroot symlink: ' + path)
    if p.is_dir():
        if any(path == d or path.startswith(d + '/') for d in owned):
            # The service needs to read its EnvironmentFile, not other users.
            print('%dir ' + path)
        continue
    if path.endswith('.sample') or '/systemd/' in path:
        directive = ''
    elif path.startswith('/etc/liner-notes/'):
        group='liner_notes_web' if role=='web' else 'liner_notes_linkctl' if p.name=='link.toml' else 'liner_notes'
        directive = '%attr(0640,root,'+group+') %config(noreplace) '
    elif path.startswith('/usr/share/liner-notes/') and p.suffix == '.txt':
        directive = '%config(noreplace) '
    elif p.name == 'LICENSE':
        directive = '%license '
    else:
        directive = ''
    print(directive + path)
