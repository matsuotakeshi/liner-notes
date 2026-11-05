#!/usr/bin/env python3
"""Install Linux programs/assets; preserve configuration and never start services."""
import argparse,json,os,re,shlex,sys,tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def install(destdir,prefix,python,unitdir='/usr/local/lib/systemd/system'):
    if sys.platform!='linux':raise ValueError('This installer targets Linux; use ports on FreeBSD')
    if sys.version_info<(3,11):raise ValueError('Run this installer with Python 3.11 or later')
    for value in (prefix,python,unitdir):
        if not re.fullmatch(r'/[A-Za-z0-9_./-]+',value) or '..' in Path(value).parts:
            raise ValueError('Use absolute paths without whitespace or parent traversal')
    if not os.path.isfile(python) or not os.access(python,os.X_OK):raise ValueError('Python interpreter is not executable')
    stage=Path(destdir).absolute() if destdir else Path('/')
    if stage.is_symlink():raise ValueError('Staging root must not be a symlink')
    share=prefix+'/share/liner-notes';config='/etc/liner-notes'
    def target(path):
        out=stage/path.lstrip('/')
        for part in [out,*out.parents]:
            if part.is_symlink():raise ValueError('Refusing symlink destination: '+str(part))
            if part==stage:break
        return out
    def put(path,data,mode=0o644,sample=False,update_default=False):
        out=target(path);out.parent.mkdir(parents=True,exist_ok=True)
        if sample:
            previous=out.with_name(out.name+'.sample')
            replace_default=(update_default and out.is_file() and previous.is_file()
                             and out.read_bytes()==previous.read_bytes())
            put(path+'.sample',data,mode)
            if out.exists() and not replace_default:return
        if out.exists() and not out.is_file():raise ValueError('Destination is not a regular file: '+str(out))
        fd,name=tempfile.mkstemp(prefix='.install-',dir=out.parent)
        try:
            with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fchmod(f.fileno(),mode);os.fsync(f.fileno())
            os.replace(name,out)
        finally:
            if os.path.exists(name):os.unlink(name)
    for source,installed in [('runtime','sbin/liner-notes'),('memberctl','bin/liner-notes-memberctl'),('vaultctl','bin/liner-notes-vaultctl'),('dbconvert','bin/liner-notes-dbconvert')]:
        put(prefix+'/'+installed,(ROOT/source).read_bytes(),0o755)
    for p in (ROOT/'scripts').glob('*.py'):
        put(share+'/scripts/'+p.name,p.read_bytes(),0o755 if p.name=='door_guess.py' else 0o644)
    put(share+'/scripts/bbs_transport.py',(ROOT/'webui/bbs_transport.py').read_bytes())
    put(share+'/scripts/check-resume.py',(ROOT/'ports/test-resume.py').read_bytes())
    put(share+'/scripts/door_clans.sh',(ROOT/'scripts/door_clans.sh').read_bytes(),0o755)
    def wrapper(name,script,setup='',arguments=''):
        text='#!/bin/sh\n'+setup+'exec '+shlex.quote(python)+' -B '+shlex.quote(share+'/scripts/'+script)+' '+arguments+' "$@"\n'
        put(prefix+'/bin/'+name,text.encode(),0o755)
    wrapper('liner-notes-admin','maintenance.py','export LINER_NOTES_TOOLS_DIR='+shlex.quote(prefix+'/bin')+'\n')
    wrapper('liner-notes-check-resume','check-resume.py')
    wrapper('liner-notes-serial','serial_connector.py')
    wrapper('liner-notes-link','link_service.py')
    wrapper('liner-notes-zmodem','zmodem_host.py',
            'export LINER_NOTES_RESUME_RECEIPT="${LINER_NOTES_RESUME_RECEIPT:-/var/lib/liner-notes/lrz-resume.json}"\n',
            '--receiver "${LINER_NOTES_LRZ:-'+prefix+'/bin/lrz}" --sender "${LINER_NOTES_LSZ:-'+prefix+'/bin/lsz}" --spool-root "${LINER_NOTES_SPOOL_ROOT:-/var/lib/liner-notes/spool}" --')
    for directory in ('help','menus'):
        for p in (ROOT/directory).rglob('*.txt'):put(share+'/'+p.relative_to(ROOT).as_posix(),p.read_bytes(),sample=True,update_default=directory=='help')
    for name in ('banner','sysinfo','goodbye','welcome','systemnews','birthday','greeting-06','greeting-12','greeting-18'):
        put(share+'/'+name+'.txt',(ROOT/(name+'.txt')).read_bytes(),sample=True)
    for name in ('limits','vault-limits','doors'):
        content=(ROOT/(name+'.example.conf')).read_bytes()
        if name=='doors':content=('# id|minimum-level|seconds|absolute-executable|absolute-work-directory|title\n# 1|0|600|'+share+'/scripts/door_guess.py|/var/lib/liner-notes/games|Number guessing\n').encode()
        put(config+'/'+name+'.conf',content,sample=True)
    docdir=prefix+'/share/doc/liner-notes'
    book=ROOT/'docs/book'
    if book.is_dir():
        for p in book.rglob('*'):
            if p.is_file():put(docdir+'/'+p.relative_to(book).as_posix(),p.read_bytes())
    else:
        # A Git checkout can install the canonical Markdown without a renderer.
        manual=ROOT/'docs/manual'
        manifest=json.loads((manual/'manifest.json').read_text(encoding='utf8'))
        index='# Liner Notes '+manifest['version']+' 統合マニュアル\n\n'
        for no,title in enumerate(manifest['chapters'],1):
            index+='## '+title+'\n\n'
            for page in manifest['pages']:
                if page['chapter']==no:index+='- ['+page['title']+'](manual/'+page['id']+'.md)\n'
            index+='\n'
        for page in manifest['pages']:
            name=page['id']+'.md';put(docdir+'/manual/'+name,(manual/name).read_bytes())
        put(docdir+'/manual/LICENSE',(ROOT/'LICENSE').read_bytes())
        put(docdir+'/index.md',index.encode('utf8'))
    put(config+'/link.toml',(ROOT/'link.example.toml').read_bytes(),sample=True)
    put(config+'/link-accounts.conf',(ROOT/'link-accounts.example.conf').read_bytes(),sample=True)
    put(share+'/LICENSE',(ROOT/'LICENSE').read_bytes())
    put(share+'/THIRD_PARTY.md',(ROOT/'THIRD_PARTY.md').read_bytes())
    for name,path in [('liner-notes.env','/etc/liner-notes/liner-notes.env'),('liner-notes.service',unitdir+'/liner-notes.service')]:
        content=(ROOT/'linux'/name).read_text().replace('@PREFIX@',prefix).encode()
        put(path,content,0o640 if name.endswith('.env') else 0o644,sample=True)
    print('Installed programs and samples. Existing settings preserved; accounts, data and services unchanged.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--destdir',default='');p.add_argument('--prefix',default='/usr/local');p.add_argument('--python',default=sys.executable)
    p.add_argument('--unitdir',default='/usr/local/lib/systemd/system')
    a=p.parse_args()
    try:install(a.destdir,a.prefix,a.python,a.unitdir)
    except (OSError,ValueError) as e:p.exit(1,str(e)+'\n')
