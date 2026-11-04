"""Offline, copy-only SQLite migration and logical backup for Liner notes.

The authoritative DB stores portable streams as structured rows or legacy
4 KiB blocks, plus relational settings. Derived indexes and public/private
file payloads are separate. Logical exports retain their storage layout and
compatibility with the audited compactors.
"""
from pathlib import Path
import contextlib,hashlib,json,os,shutil,sqlite3,tempfile,subprocess
import maintenance

BLOCK=4096
STREAM_LIST='.liner-sqlite-streams.json'
LAYOUT='.liner-storage-layout.json'
TABLES={
 'board_deletions':'board INTEGER PRIMARY KEY,actor INTEGER,depth INTEGER,done INTEGER',
 'menu_preferences':'id INTEGER PRIMARY KEY,value INTEGER',
 'guest_boards':'id INTEGER PRIMARY KEY,value INTEGER',
 'news_scoped':'user INTEGER,kind INTEGER,scope TEXT,stamp INTEGER NOT NULL,PRIMARY KEY(user,kind,scope)',
 'news':'user INTEGER,kind INTEGER,stamp INTEGER NOT NULL,PRIMARY KEY(user,kind)',
 'delegated':'board INTEGER,gid INTEGER,PRIMARY KEY(board,gid)',
 'group_moves':'board INTEGER PRIMARY KEY,actor INTEGER,source INTEGER,destination INTEGER,done INTEGER',
 'applications':'user INTEGER,field TEXT,value TEXT,PRIMARY KEY(user,field)',
 'link_articles':'board INTEGER NOT NULL,origin TEXT NOT NULL,article TEXT NOT NULL,local_id INTEGER UNIQUE NOT NULL,revision INTEGER NOT NULL,kind TEXT NOT NULL,author TEXT NOT NULL,alias TEXT NOT NULL,created INTEGER NOT NULL,parent INTEGER NOT NULL,title TEXT NOT NULL,body_hash TEXT NOT NULL,done INTEGER NOT NULL,actor INTEGER NOT NULL,PRIMARY KEY(board,origin,article)',
}
MAGICS={b'RTMEMB01',b'LNMAIL01',b'LNVLT001',b'LNBDP001',b'LNBMOV01',b'LNBACC01',b'LNCAT001',b'LNSIG001',b'LNDOWN01',b'LNCHAT01',b'LNULOCK1',b'LNUSER01',b'LNPROF01',b'RTMOD001',b'RTEXTRA1',b'LNACTV01'}|{f'RTBOARD{i}'.encode() for i in range(1,6)}

@contextlib.contextmanager
def locked(database):
 import fcntl
 database=Path(database);maintenance.need(database.is_file() and not database.is_symlink(),'Missing or unsafe database')
 fd=os.open(str(database)+'.lock',os.O_RDWR|os.O_CREAT|os.O_NOFOLLOW,0o600)
 try:
  fcntl.lockf(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
  with sqlite3.connect(database.as_uri()+'?mode=ro',uri=True) as db:
   maintenance.need(db.execute('pragma integrity_check').fetchall()==[('ok',)],'SQLite integrity check failed')
   maintenance.need(db.execute('select version from storage_meta where id=1').fetchone() in ((1,),(2,),(3,)),'Unknown storage schema')
   yield db
 finally:os.close(fd)

def copy_instance(root,destination,exclude=()):
 root=Path(root).resolve();destination=Path(destination);files,directories=maintenance.inventory(root)
 destination.mkdir(mode=0o700)
 for name in directories:(destination/name).mkdir(mode=0o700,parents=True,exist_ok=True)
 for name in files:
  if name in exclude or any(name.startswith(x+'-') for x in exclude):continue
  target=destination/name;target.parent.mkdir(mode=0o700,parents=True,exist_ok=True);shutil.copy2(root/name,target)
  target.chmod(0o700 if target.stat().st_mode&0o111 else 0o600)
 return destination

def export_streams(db,destination,board):
 names=[]
 for ident,name,length in db.execute('select id,name,length from streams order by id'):
  name=maintenance.relative(name);maintenance.need(length>=0,'Negative stream length')
  target=destination/name;target.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
  remaining=length;number=0
  with target.open('wb') as f:
   target.chmod(0o600)
   structured=db.execute("SELECT 1 FROM sqlite_master WHERE name='record_streams'").fetchone()
   record=db.execute('SELECT kind,magic FROM record_streams WHERE stream=?',(ident,)).fetchone() if structured else None
   if record:
    import structured_db
    structured_db.export(db,ident,*record,length,f)
   else:
    for index,data in db.execute('select number,value from blocks where stream=? order by number',(ident,)):
     maintenance.need(index==number and len(data)==BLOCK and remaining>0,'Missing, extra or invalid storage block')
     n=min(remaining,BLOCK);f.write(data[:n]);remaining-=n;number+=1
    maintenance.need(remaining==0,'Truncated storage stream')
   f.flush();os.fsync(f.fileno())
  names.append(name)
 features=destination/(maintenance.relative(board)+'.features.sqlite')
 if features.exists():features.unlink()
 with sqlite3.connect(features) as out:
  features.chmod(0o600)
  for table,columns in TABLES.items():
   out.execute(f'CREATE TABLE {table}({columns})')
   if db.execute("select 1 from sqlite_master where type='table' and name=?",(table,)).fetchone():
    for row in db.execute(f'SELECT * FROM {table}'):
     out.execute(f'INSERT INTO {table} VALUES({",".join("?" for _ in row)})',row)
 (destination/STREAM_LIST).write_text(json.dumps(names),encoding='utf8');(destination/STREAM_LIST).chmod(0o600)
 return names

def export_instance(root,database,board,destination):
 root=Path(root).resolve();database=Path(database).resolve();destination=Path(destination).absolute()
 maintenance.outside(destination,root);relative_db=database.relative_to(root).as_posix()
 with locked(database) as db:
  copy_instance(root,destination,(relative_db,relative_db+'.lock'))
  names=export_streams(db,destination,board)
  (destination/LAYOUT).write_text(json.dumps({'schema':db.execute('SELECT version FROM storage_meta WHERE id=1').fetchone()[0]}),encoding='utf8')
  (destination/LAYOUT).chmod(0o600)
 return {'operation':'db-export','directory':str(destination),'streams':len(names)}

def import_copy(root,board,members,database='liner.sqlite',structured=False,tools_dir=None):
 root=Path(root);database=maintenance.relative(database);target=root/database
 maintenance.need(not target.exists(),'Database output already exists')
 files,_=maintenance.inventory(root,allow_incomplete=True)
 if (root/STREAM_LIST).exists():
  names=json.loads((root/STREAM_LIST).read_text(encoding='utf8'))
  maintenance.need(isinstance(names,list) and len(names)==len(set(names)),'Invalid stream inventory')
  names=[maintenance.relative(n) for n in names]
 else:
  names=[]
  for name in files:
   if name.endswith('.sqlite'):continue
   with (root/name).open('rb') as f:magic=f.read(8)
   if magic in MAGICS:names.append(name)
 maintenance.need(board in names and members in names,'Board/member streams missing')
 target.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
 if structured:
  # Reuse the runtime's record decoder rather than a second Python codec.
  directory=Path(tools_dir or os.environ.get('LINER_NOTES_TOOLS_DIR') or Path(__file__).resolve().parents[1])
  converter=directory/'liner-notes-dbconvert'
  if not converter.exists():converter=directory/'dbconvert'
  maintenance.need(converter.is_file() and os.access(converter,os.X_OK),'Build/install dbconvert before structured migration')
  with tempfile.TemporaryDirectory(prefix='.structured-',dir=target.parent) as tmp:
   converted=Path(tmp)/target.name
   subprocess.run([str(converter.resolve()),str(converted.resolve()),str(root.resolve())],input=('\n'.join(names)+'\n').encode('utf8'),check=True)
   # Refuse overwrite even if a destination appeared while conversion ran.
   os.link(converted,target);converted.unlink()
 else:
  fd=os.open(target,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600);os.close(fd)
 with sqlite3.connect(target) as db:
  if not structured:db.executescript('PRAGMA synchronous=FULL;CREATE TABLE storage_meta(id INTEGER PRIMARY KEY CHECK(id=1),version INTEGER NOT NULL,uuid BLOB NOT NULL);INSERT INTO storage_meta VALUES(1,1,randomblob(16));CREATE TABLE streams(id INTEGER PRIMARY KEY,name TEXT UNIQUE NOT NULL,length INTEGER NOT NULL DEFAULT 0,revision INTEGER NOT NULL DEFAULT 0);CREATE TABLE blocks(stream INTEGER NOT NULL REFERENCES streams(id) ON DELETE CASCADE,number INTEGER NOT NULL,value BLOB NOT NULL CHECK(length(value)=4096),PRIMARY KEY(stream,number)) WITHOUT ROWID;')
  db.execute('CREATE TABLE storage_imports(name TEXT PRIMARY KEY)')
  for name in (() if structured else names):
   path=root/name;maintenance.need(path.is_file() and not path.is_symlink(),'Missing stream: '+name)
   ident=db.execute('INSERT INTO streams(name,length,revision) VALUES(?,?,1)',(name,path.stat().st_size)).lastrowid
   number=0
   with path.open('rb') as f:
    while data:=f.read(BLOCK):db.execute('INSERT INTO blocks VALUES(?,?,?)',(ident,number,data.ljust(BLOCK,b'\0')));number+=1
  features=root/(board+'.features.sqlite')
  with contextlib.ExitStack() as stack:
   source=stack.enter_context(sqlite3.connect(features.as_uri()+'?mode=ro',uri=True)) if features.exists() else None
   for table,columns in TABLES.items():
    db.execute(f'CREATE TABLE {table}({columns})')
    if source and source.execute("select 1 from sqlite_master where name=? and type='table'",(table,)).fetchone():
     for row in source.execute(f'SELECT * FROM {table}'):db.execute(f'INSERT INTO {table} VALUES({",".join("?" for _ in row)})',row)
  db.execute("INSERT INTO storage_imports VALUES('features')");db.commit()
  maintenance.need(db.execute('pragma integrity_check').fetchone()==('ok',),'Migrated database is invalid')
  # Verify every byte against the source, not just row counts.
  for ident,name,length in db.execute('select id,name,length from streams'):
   h=hashlib.sha256();remaining=length
   record=db.execute('SELECT kind,magic FROM record_streams WHERE stream=?',(ident,)).fetchone() if structured else None
   if record:
    import structured_db
    class Digest:
     def write(self,data):h.update(data)
    structured_db.export(db,ident,*record,length,Digest());remaining=0
   else:
    for (data,) in db.execute('select value from blocks where stream=? order by number',(ident,)):
     n=min(remaining,BLOCK);h.update(data[:n]);remaining-=n
   with (root/name).open('rb') as f:expected=hashlib.file_digest(f,'sha256').digest()
   maintenance.need(remaining==0 and h.digest()==expected,'Migration hash mismatch: '+name)
 maintenance.sync_directory(target.parent)
 return {'operation':'db-import','database':str(target),'streams':len(names),'layout':'structured' if structured else 'blocks','verified':'SHA-256 of every stream'}

def backup(root,database,board,members,archive):
 root=Path(root).resolve();archive=Path(archive).absolute();maintenance.outside(archive,root)
 # The service account owns root, not necessarily its parent (/var/db).
 # Keep the private working copy beside the explicitly selected output.
 with tempfile.TemporaryDirectory(prefix='liner-db-backup-',dir=archive.parent) as tmp:
  export_instance(root,database,board,Path(tmp)/'instance')
  result=maintenance.backup(Path(tmp)/'instance',board,members,archive)
  result['operation']='db-backup';return result

def restore(archive,destination,compact=False,reclaim=False,all_stores=False,tools_dir=None,database='liner.sqlite',structured=None):
 result=maintenance.restore(archive,destination,compact=compact,all_stores=all_stores,tools_dir=tools_dir,reclaim_withdrawn=reclaim)
 manifest=result['manifest'];marker=Path(destination)/maintenance.INCOMPLETE;marker.write_text('SQLite migration incomplete\n')
 try:
  layout=Path(destination)/LAYOUT
  if structured is None:
   schema=json.loads(layout.read_text(encoding='utf8'))['schema'] if layout.exists() else 1
   maintenance.need(schema in (1,2,3),'Unknown backup storage layout')
   structured=schema>=2
  result['database']=import_copy(Path(destination).resolve(),manifest['board'],manifest['members'],database,structured,tools_dir)
 except Exception:
  marker.write_text('SQLite migration failed: do not start this copy\n');raise
 marker.unlink();maintenance.sync_directory(destination)
 result['operation']='db-compact' if compact else 'db-restore';return result

def structure(root,database,board,members,destination,tools_dir=None):
 root=Path(root).resolve();destination=Path(destination).absolute()
 maintenance.outside(destination,root)
 maintenance.need(not destination.exists(),'Destination already exists')
 with tempfile.TemporaryDirectory(prefix='liner-structure-',dir=destination.parent) as tmp:
  archive=Path(tmp)/'source.tar.gz'
  backup(root,database,board,members,archive)
  result=restore(archive,destination,tools_dir=tools_dir,structured=True)
 result['operation']='db-structure';return result
