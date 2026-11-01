"""Manage private, inactive transfer staging without touching published payloads."""
import argparse,contextlib,fcntl,hashlib,json,os,re,shutil,stat,time,getpass,sys
from pathlib import Path

NAME=re.compile(r"(?:pending-([a-f0-9]{64})|retained-([a-f0-9]{64})-[a-z0-9_]+)")
LEGACY=re.compile(r'liner-zmodem-[a-zA-Z0-9_-]+')
def need(ok,message):
    if not ok:raise ValueError(message)
def private(path,directory=False):
    st=path.lstat()
    need((stat.S_ISDIR(st.st_mode) if directory else stat.S_ISREG(st.st_mode) and st.st_nlink==1) and not st.st_mode&0o077 and (os.geteuid()==0 or st.st_uid==os.geteuid()),'Unsafe private entry: '+str(path))
    return st
@contextlib.contextmanager
def root_lock(root):
    root=Path(root).absolute();private(root,True)
    fd=os.open(root,os.O_RDONLY|os.O_DIRECTORY|os.O_NOFOLLOW)
    try:
        fcntl.flock(fd,fcntl.LOCK_EX);yield root
    finally:os.close(fd)
@contextlib.contextmanager
def entry_lock(root,name):
    need(NAME.fullmatch(name) is not None,'Invalid transfer ID')
    work=root/name;private(work,True)
    fd=os.open(work/'lock',os.O_RDWR|os.O_NOFOLLOW)
    try:
        private(work/'lock');fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB);yield work
    finally:os.close(fd)
def inspect(work):
    count=0;size=0
    for base,dirs,files in os.walk(work,followlinks=False):
        for n in dirs:private(Path(base)/n,True)
        for n in files:
            st=private(Path(base)/n);count+=1;size+=st.st_size
            need(count<=100,'Unexpected spool file count')
    meta=work/'transfer.json';need(private(meta).st_size<=8192,'Oversized transfer metadata')
    saved=json.loads(meta.read_text(encoding='utf8'));identity=saved.get('identity')
    need(isinstance(identity,list) and len(identity) in (5,6) and isinstance(saved.get('received'),bool),'Invalid transfer metadata')
    key=hashlib.sha256(json.dumps(identity).encode()).hexdigest()
    match=NAME.fullmatch(work.name);need(key in match.groups(),'Transfer identity mismatch')
    return {'id':work.name,'kind':'retained' if work.name.startswith('retained-') else 'pending',
            'phase':'publication-pending' if saved['received'] else 'interrupted',
            'user':identity[2],'namespace':identity[3],'board':identity[4],
            'bytes':size,'age_seconds':max(0,int(time.time()-meta.stat().st_mtime)),'key':key}

def details(work):
    row=inspect(work);row.pop('key')
    saved=json.loads((work/'transfer.json').read_text(encoding='utf8'))
    files=[]
    for name,metadata in saved.get('manifest',{}).items():
        need(isinstance(name,str) and name not in ('.','..') and not re.search(r'[/\\]',name),'Invalid file name')
        need(isinstance(metadata,list) and len(metadata)==2,'Invalid manifest')
        source=work/'data'/name;private(source)
        with source.open('rb') as f:digest=hashlib.file_digest(f,'sha256').hexdigest()
        need(metadata==[source.stat().st_size,digest],'Staged file changed')
        state=work/(hashlib.sha256(name.encode()).hexdigest()+'.json')
        item=dict(name=name,bytes=metadata[0],sha256=digest,phase='received',reservation=None)
        if state.exists() or state.is_symlink():
            need(private(state).st_size<=8192,'Oversized state')
            value=json.loads(state.read_text(encoding='utf8'))
            need(isinstance(value,dict) and value.get('size')==metadata[0] and value.get('sha256')==digest,'State does not match manifest')
            item.update(phase=value.get('phase'),reservation=value.get('id'))
        files.append(item)
    row['files']=files
    return row,saved

def reconcile(root,name,host,port,user,password,bindings=(),apply=False,cleanup=False):
    """Authenticate as the original owner; query status without beginning uploads."""
    from transfer_connectors import CONNECTORS
    from vault_zmodem import _upload
    with root_lock(root) as root,entry_lock(root,name) as work:
        report,saved=details(work);identity=saved['identity']
        need(identity[:3]==[host,port,user],'Explicit endpoint/member does not match transfer')
        need(saved['received'],'Wire transfer is not complete; use ZRESUME')
        factory=CONNECTORS.get(identity[3]);need(factory is not None,'Unsupported connector')
        chosen={}
        for binding in bindings:
            filename,sep,number=binding.rpartition('=')
            need(sep and number.isdecimal() and 0<int(number)<=0x7ffffffe and filename not in chosen,'Use unique filename=reservation-ID bindings')
            chosen[filename]=int(number)
        need(set(chosen)<={f['name'] for f in report['files']},'Unknown binding filename')
        changes=[]
        c=factory(host,port,user,password)
        try:
            for item in report['files']:
                state_path=work/(hashlib.sha256(item['name'].encode()).hexdigest()+'.json')
                if not state_path.exists():
                    need(item['name'] not in chosen,'No start record: cannot bind a reservation')
                    item['remote']='NOT_STARTED';continue
                state=_upload.StateFile(state_path)
                _upload.check_endpoint(state.data,host,port,user,factory)
                need(state.data.get('category','')==identity[4],'File board mismatch')
                need(state.data.get('name')==item['name'],'Saved filename mismatch')
                ident=state.data.get('id')
                if item['name'] in chosen:
                    need(ident is None and state.data.get('phase')=='starting','Only an unknown start can be explicitly bound')
                    ident=chosen[item['name']]
                if ident is None:item['remote']='UNKNOWN_START' if state.data.get('phase')=='starting' else 'NOT_STARTED';continue
                phase,offset,size,digest=c.status(ident)
                need((size,digest)==(item['bytes'],item['sha256']) and phase in ('ACTIVE','COMMITTED','CANCELLED'),'Reservation metadata mismatch')
                need(0<=offset<=size and (phase!='COMMITTED' or offset==size),'Invalid reservation offset')
                item.update(remote=phase,offset=offset,reservation=ident)
                if phase in ('ACTIVE','COMMITTED'):
                    state.data.update(id=ident,phase='committed' if phase=='COMMITTED' else 'active')
                    changes.append(state)
        finally:c.close()
        all_committed=bool(report['files']) and all(x.get('remote')=='COMMITTED' for x in report['files'])
        if cleanup:need(all_committed,'Cleanup requires every file to be confirmed committed')
        # Do not modify any receipt until every query and cleanup condition passed.
        if apply:
            for state in changes:state.save()
        report.update(action='reconcile',applied=apply,
                      all_committed=all_committed)
        if cleanup:
            if apply:
                shutil.rmtree(work)
                fd=os.open(root,os.O_RDONLY|os.O_DIRECTORY)
                try:os.fsync(fd)
                finally:os.close(fd)
            report['cleanup_applied']=apply
        return report
def manage(root,action,name=None,apply=False):
    with root_lock(root) as root:
        if action=='list':
            result=[]
            for p in sorted(root.iterdir()):
                if LEGACY.fullmatch(p.name):
                    try:
                        private(p,True)
                        result.append({'id':p.name,'phase':'legacy-recovery','modifiable':False,
                                       'instruction':'Use show; recover with the original upload state. Active legacy processes cannot be identified safely.'})
                    except (OSError,ValueError) as error:result.append({'id':p.name,'phase':'invalid','error':str(error)})
                    continue
                if not NAME.fullmatch(p.name):continue
                try:
                    with entry_lock(root,p.name) as work:row=inspect(work);row.pop('key');result.append(row)
                except BlockingIOError:result.append({'id':p.name,'phase':'active'})
                except (OSError,ValueError,KeyError) as error:result.append({'id':p.name,'phase':'invalid','error':str(error)})
            return result
        if action=='show' and LEGACY.fullmatch(name or ''):
            work=root/name;private(work,True);state=work/'upload.json'
            need(private(state).st_size<=8192,'Oversized legacy state')
            saved=json.loads(state.read_text(encoding='utf8'))
            return dict(id=name,phase='legacy-recovery',modifiable=False,
                        file={k:saved.get(k) for k in ('name','size','sha256','id','phase','connector','user','category')})
        with entry_lock(root,name) as work:
            if action=='show':return details(work)[0]
            row=inspect(work);key=row.pop('key')
            need(row['phase']=='interrupted','Publication is uncertain; use ZRESUME to reconcile before removal/restoration')
            if action=='restore':
                need(row['kind']=='retained','Only retained transfers can be restored')
                target=root/('pending-'+key);need(not target.exists() and not target.is_symlink(),'Current pending transfer already exists')
                if apply:os.rename(work,target)
            elif action=='remove':
                if apply:shutil.rmtree(work)
            else:raise ValueError('Unknown action')
            if apply:
                fd=os.open(root,os.O_RDONLY|os.O_DIRECTORY)
                try:os.fsync(fd)
                finally:os.close(fd)
            return dict(row,action=action,applied=apply)
def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('list','show','restore','remove','reconcile'));p.add_argument('--root',required=True)
    p.add_argument('id',nargs='?');p.add_argument('--apply',action='store_true',help='Perform selected mutation; default only previews')
    p.add_argument('--host');p.add_argument('--port',type=int);p.add_argument('--user')
    p.add_argument('--password-stdin',action='store_true');p.add_argument('--bind',action='append',default=[],metavar='FILENAME=ID')
    p.add_argument('--cleanup',action='store_true',help='Reconcile: remove local staging only when every remote file is committed; requires --apply to mutate')
    args=p.parse_args(argv)
    if args.action!='list' and not args.id:p.error('A transfer ID from list is required')
    if args.action=='reconcile' and not all((args.host,args.port,args.user)):p.error('reconcile requires explicit --host, --port and --user of the original uploader')
    if args.cleanup and args.action!='reconcile':p.error('--cleanup is only valid with reconcile')
    try:
        if args.action=='reconcile':
            password=sys.stdin.readline().rstrip('\r\n') if args.password_stdin else getpass.getpass('Original uploader password: ')
            result=reconcile(args.root,args.id,args.host,args.port,args.user,password,args.bind,args.apply,args.cleanup)
        else:result=manage(args.root,args.action,args.id,args.apply)
        print(json.dumps(result,ensure_ascii=True,indent=2))
    except (OSError,ValueError,KeyError) as error:p.exit(1,str(error)+'\n')
if __name__=='__main__':main()
