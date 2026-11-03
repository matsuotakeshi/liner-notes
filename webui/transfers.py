"""Private resume metadata only. File bytes and permissions remain owned by the BBS."""
import hashlib
import json
import os
from pathlib import Path
import tempfile

class ResumeRecords:
    def __init__(self, spool, upstream):
        self.root=Path(spool)/'resumes';self.root.mkdir(mode=0o700,exist_ok=True)
        if self.root.is_symlink():raise ValueError('Resume directory must not be a symlink')
        self.target=hashlib.sha256(f"{upstream['host']}:{upstream['port']}".encode()).hexdigest()[:16]

    def path(self,state,kind,ident):
        if kind not in ('mail','public') or not 1<=int(ident)<=2147483646:raise ValueError('Invalid resume record')
        return self.root/f"{self.target}-{int(state['user'])}-{kind}-{int(ident)}.json"

    def save(self,state,record):
        record={**record,'user':state['user'],'username':state['username'],'target':self.target}
        destination=self.path(state,record['kind'],record['id'])
        fd,temp=tempfile.mkstemp(prefix='.resume-',dir=self.root)
        try:
            with os.fdopen(fd,'w',encoding='utf-8') as f:json.dump(record,f,ensure_ascii=False);f.flush();os.fsync(f.fileno())
            os.replace(temp,destination)
        finally:Path(temp).unlink(missing_ok=True)

    def remove(self,state,kind,ident):self.path(state,kind,ident).unlink(missing_ok=True)

    def list(self,state):
        if not state.get('user'):return []
        records=[]
        for path in self.root.glob(f"{self.target}-{int(state['user'])}-*.json"):
            if path.is_symlink():continue
            try:
                row=json.loads(path.read_text(encoding='utf-8'))
                if row.get('user')==state['user'] and row.get('username')==state['username'] and row.get('target')==self.target:records.append(row)
            except (OSError,ValueError):continue
        return records
