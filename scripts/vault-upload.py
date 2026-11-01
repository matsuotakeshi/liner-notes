#!/usr/bin/env python3
"""Upload via a replaceable connector; persist an ID for explicit resume/cancel."""
import argparse
import getpass
import hashlib
import json
import os
import pathlib
import stat
import sys
import tempfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from transfer_connectors import LineConnector, CONNECTORS, RemoteError


def metadata(name, description):
    for text,maximum in [(name,100),(description,240)]:
        if len(text.encode('utf-8'))>maximum or any(ord(c)<32 or 127<=ord(c)<=159 for c in text):
            raise ValueError('Invalid name or description')
    if not name or name in ('.','..') or any(c in name for c in '/\\:'):raise ValueError('Invalid display name')


def sync_directory(path):
    if os.name=='posix':
        fd=os.open(path,os.O_RDONLY|getattr(os,'O_DIRECTORY',0))
        try:os.fsync(fd)
        finally:os.close(fd)


class StateFile:
    def __init__(self, path, initial=None):
        self.path=pathlib.Path(path)
        if self.path.is_symlink():raise ValueError('State file must not be a symlink')
        if initial is not None:
            fd=os.open(self.path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
            with os.fdopen(fd,'w',encoding='utf-8') as f:
                f.write(self.encode(initial));f.flush();os.fsync(f.fileno())
            sync_directory(self.path.parent)
        if self.path.stat().st_size>8192:raise ValueError('State file too large')
        self.saved=self.path.read_text(encoding='utf-8');self.data=json.loads(self.saved)
        if not isinstance(self.data,dict) or self.data.get('version')!=1:raise ValueError('Unsupported state file')
    @staticmethod
    def encode(data):return json.dumps(data,ensure_ascii=False,sort_keys=True)+'\n'
    def save(self):
        if self.path.is_symlink() or self.path.read_text(encoding='utf-8')!=self.saved:
            raise ValueError('State file changed by another process')
        data=self.encode(self.data);fd,name=tempfile.mkstemp(prefix='.upload-state-',dir=self.path.parent)
        try:
            with os.fdopen(fd,'w',encoding='utf-8') as f:f.write(data);f.flush();os.fsync(f.fileno())
            os.replace(name,self.path);sync_directory(self.path.parent);self.saved=data
        finally:
            if os.path.exists(name):os.unlink(name)


def check_endpoint(state, host, port, user, factory):
    if any(state.get(key)!=value for key,value in [('host',host),('port',port),('user',user),('connector',factory.name)]):
        raise ValueError('State belongs to a different endpoint, account or connector')
    ident=state.get('id')
    if ident is not None and (type(ident) is not int or not 1<=ident<=0x7ffffffe):raise ValueError('Invalid saved transfer ID')


def upload(host,port,user,password,source,state_path,*,resume=False,name=None,description=None,category=None,connector_factory=LineConnector,progress=None):
    source=pathlib.Path(source).resolve()
    fd=os.open(source,os.O_RDONLY|getattr(os,'O_BINARY',0)|getattr(os,'O_NONBLOCK',0))
    with os.fdopen(fd,'rb') as f:
        st=os.fstat(f.fileno())
        if not stat.S_ISREG(st.st_mode) or not 0<=st.st_size<=1024*1024*1024:raise ValueError('Source must be a regular file of at most 1 GiB')
        size=st.st_size;digest=hashlib.sha256();total=0
        while True:
            chunk=f.read(65536)
            if not chunk:break
            digest.update(chunk);total+=len(chunk)
            if total>size:raise ValueError('Source size changed')
        if total!=size:raise ValueError('Source size changed')
        expected=digest.hexdigest();f.seek(0)
        if resume:
            state=StateFile(state_path);check_endpoint(state.data,host,port,user,connector_factory)
            for key,value in [('source',str(source)),('size',size),('sha256',expected)]:
                if state.data.get(key)!=value:raise ValueError('Source does not match the saved transfer')
            saved_name=state.data.get('name');saved_description=state.data.get('description')
            if not isinstance(saved_name,str) or not isinstance(saved_description,str):raise ValueError('Invalid saved metadata')
            if (name is not None and name!=saved_name) or (description is not None and description!=saved_description):raise ValueError('Metadata does not match saved transfer')
            saved_category=state.data.get('category','')
            if not isinstance(saved_category,str) or (category is not None and category!=saved_category):raise ValueError('Category does not match saved transfer')
            category=saved_category
            name,description=saved_name,saved_description
            metadata(name,description)
            if state.data.get('phase') not in ('prepared','starting','active','committed'):raise ValueError('Transfer cannot be resumed')
            if state.data.get('id') is None and state.data.get('phase')!='prepared':
                raise ValueError('Start outcome is unknown; ask the administrator to locate the reservation in FILEAUDIT. No duplicate start was sent.')
        else:
            name=name if name is not None else source.name;description=description or '';metadata(name,description)
            state=StateFile(state_path,{'version':1,'connector':connector_factory.name,'host':host,'port':port,'user':user,
                'source':str(source),'size':size,'sha256':expected,'name':name,'description':description,'category':category or '', 'id':None,'phase':'prepared'})
        c=connector_factory(host,port,user,password)
        try:
            if category:
                if not isinstance(c,LineConnector) and not hasattr(c,'upload_category'):raise ValueError('Connector does not support file boards')
                c.upload_category=category
            if state.data['id'] is None:
                state.data['phase']='starting';state.save()
                try:ident=c.begin(size,expected,name,description)
                except RemoteError as error:
                    if str(error) in ('ERR vault busy','ERR vault limit','ERR vault denied','ERR vault syntax','ERR login required','ERR busy','ERR session revoked'):
                        state.data['phase']='prepared';state.save()
                    raise
                state.data.update(id=ident,phase='active');state.save()
            ident=state.data['id'];phase,offset,remote_size,remote_hash=c.status(ident)
            if (remote_size,remote_hash)!=(size,expected):raise ValueError('Remote reservation does not match the saved file')
            if phase=='CANCELLED':raise ValueError('Reservation was cancelled')
            if phase not in ('ACTIVE','COMMITTED') or not 0<=offset<=size:raise ValueError('Invalid remote state')
            if progress:progress(offset,size)
            if phase=='ACTIVE':
                f.seek(offset)
                while offset<size:
                    data=f.read(min(c.chunk_size,size-offset))
                    if not data:raise ValueError('Source changed during transfer')
                    next_offset=c.write(ident,offset,data)
                    if next_offset!=offset+len(data):raise ValueError('Invalid write position')
                    offset=next_offset
                    if progress:progress(offset,size)
            if c.finish(ident)!=(size,expected):raise ValueError('Final size or SHA-256 differs')
            state.data['phase']='committed';state.save()
            return ident,size,expected
        finally:c.close()


def cancel(host,port,user,password,state_path,connector_factory=LineConnector):
    state=StateFile(state_path);check_endpoint(state.data,host,port,user,connector_factory)
    ident=state.data.get('id')
    if ident is None:
        if state.data.get('phase') not in ('prepared','cancelled'):raise ValueError('Unknown start outcome; administrator assistance is needed')
    else:
        c=connector_factory(host,port,user,password)
        try:c.cancel(ident)
        finally:c.close()
    state.data['phase']='cancelled';state.save();return ident


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',default='127.0.0.1');p.add_argument('--port',type=int,default=2222)
    p.add_argument('--user',required=True);p.add_argument('--state',required=True)
    p.add_argument('--connector',choices=CONNECTORS,default='line-v1')
    mode=p.add_mutually_exclusive_group();mode.add_argument('--resume',action='store_true');mode.add_argument('--cancel',action='store_true')
    p.add_argument('--category');p.add_argument('--name');p.add_argument('--description');p.add_argument('--password-stdin',action='store_true')
    p.add_argument('source',nargs='?');args=p.parse_args()
    if not args.cancel and not args.source:p.error('source is required for send/resume')
    password=sys.stdin.readline().rstrip('\r\n') if args.password_stdin else getpass.getpass('BBS password: ')
    previous=[-1]
    def progress(done,total):
        percent=100 if not total else done*100//total
        if percent!=previous[0]:print(f'Upload {percent}% ({done}/{total} bytes)',file=sys.stderr);previous[0]=percent
    try:
        factory=CONNECTORS[args.connector]
        if args.cancel:
            ident=cancel(args.host,args.port,args.user,password,args.state,factory);print(f'Cancelled upload {ident}');return 0
        ident,size,digest=upload(args.host,args.port,args.user,password,args.source,args.state,resume=args.resume,name=args.name,description=args.description,category=args.category,connector_factory=factory,progress=progress)
        print(f'Published file #{ident}: {size} bytes; SHA-256 {digest}')
    except (OSError,ValueError,KeyError,TypeError) as error:
        print(f'Upload failed: {error}. Keep the state file for resume or cancellation.',file=sys.stderr);return 1
    except KeyboardInterrupt:
        print('Interrupted. Keep the state file and use --resume or --cancel.',file=sys.stderr);return 130
    return 0


if __name__=='__main__':raise SystemExit(main())
