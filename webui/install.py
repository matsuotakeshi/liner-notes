"""Stage the optional Web service; preserve configuration and never start it."""
import argparse
import os
from pathlib import Path
import re
import shlex
import tempfile

ROOT=Path(__file__).resolve().parent


def install(destdir='',prefix='/usr/local',python='/usr/local/bin/python3.12',samples_only=False,configdir=None,rc=True):
    for item in (prefix,python,*([configdir] if configdir else [])):
        if not re.fullmatch(r'/[A-Za-z0-9_./-]+',item) or '..' in Path(item).parts:raise ValueError('Use an absolute path without spaces or ..')
    stage=Path(destdir).absolute() if destdir else Path('/')
    def directory(path):
        if path.is_symlink():raise ValueError('Symlink installation target')
        if not path.exists():
            directory(path.parent)
            path.mkdir(mode=0o755);path.chmod(0o755) # independent of installer umask
        elif not path.is_dir():raise ValueError('Non-directory installation target')
    def write(path,data,mode=0o644,sample=False):
        target=stage/path.lstrip('/')
        for ancestor in (target,*target.parents):
            if ancestor.is_symlink():raise ValueError('Symlink installation target')
            if ancestor==stage:break
        directory(target.parent)
        if sample:
            write(path+'.sample',data,mode)
            if samples_only or target.exists():return
        if target.exists() and not target.is_file():raise ValueError('Non-file installation target')
        fd,temp=tempfile.mkstemp(prefix='.web-install-',dir=target.parent)
        try:
            with os.fdopen(fd,'wb') as stream:stream.write(data);stream.flush();os.fchmod(stream.fileno(),mode);os.fsync(stream.fileno())
            os.replace(temp,target)
        finally:Path(temp).unlink(missing_ok=True)
    share=prefix+'/share/liner-notes/web';etc=configdir or prefix+'/etc/liner-notes/web'
    # These directories contain public assets; config files retain their own 0640 mode.
    # Repair paths made inaccessible by older installers under umask 077.
    for name in (share,share+'/webui',share+'/webui/static',share+'/webui/templates',etc,etc+'/templates',etc+'/messages'):
        path=stage/name.lstrip('/')
        for ancestor in (path,*path.parents):
            if ancestor.is_symlink():raise ValueError('Symlink installation target')
            if ancestor==stage:break
        directory(path);path.chmod(0o755)
    for path in ROOT.rglob('*'):
        relative=path.relative_to(ROOT)
        if not path.is_file() or path.is_symlink() or '__pycache__' in relative.parts:continue
        if path.suffix=='.py' and len(relative.parts)==1 or relative.parts[0] in ('static','templates'):
            write(share+'/webui/'+relative.as_posix(),path.read_bytes())
    for path in (ROOT/'messages').glob('*.json'):write(etc+'/messages/'+path.name,path.read_bytes(),sample=True)
    for path in (ROOT/'templates').glob('*.html'):write(etc+'/templates/'+path.name,path.read_bytes(),sample=True)
    config=(ROOT/'example.toml').read_text(encoding='utf-8').replace('messages = "messages"','messages = "'+etc+'/messages"').replace('templates = "templates"','templates = "'+etc+'/templates"')
    write(etc+'/web.toml',config.encode(),0o640,sample=True)
    program='#!/bin/sh\ncd '+shlex.quote(share)+' || exit 1\nexec '+shlex.quote(python)+' -m webui "$@"\n'
    write(prefix+'/sbin/liner-notes-web',program.encode(),0o755)
    if rc:
        script=(ROOT/'deployment/liner_notes_web').read_text().replace('@PREFIX@',prefix)
        write(prefix+'/etc/rc.d/liner_notes_web',script.encode(),0o755)
    write(share+'/apache.conf.sample',(ROOT/'deployment/apache.conf').read_bytes())


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--destdir',default='');p.add_argument('--prefix',default='/usr/local');p.add_argument('--python',default='/usr/local/bin/python3.12');p.add_argument('--samples-only',action='store_true');p.add_argument('--configdir');p.add_argument('--no-rc',action='store_true');args=p.parse_args()
    install(args.destdir,args.prefix,args.python,args.samples_only,args.configdir,not args.no_rc)
