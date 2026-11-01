"""16 KiB bodies, durable drafts and copy-only offline compaction."""
import hashlib,json,os,pathlib,shutil,subprocess,sys,tempfile
from test_board import Server,post,read
from test_runtime import Client
from test_members import auth,PASSWORD
from test_talk import command
from test_mail import send,read as mail_read
from test_preview import preview,screen,MORE
from test_draft_edit import lines
from test_telnet import Client as Telnet

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    tool=pathlib.Path(__file__).resolve().parents[1]/'scripts/notes_compact.py'
    def compact(kind,source,target=None,okay=True):
        result=subprocess.run([sys.executable,str(tool),kind,str(source),str(target) if target else '--check'],capture_output=True,text=True)
        assert (result.returncode==0)==okay,(result.stdout,result.stderr)
        return json.loads(result.stdout) if okay else None
    body='\n'.join(['x'*1022]*15+['y'*1013+'END-LARGE'])
    expected=body.replace('\n','\r\n')+'\r\n';assert len(expected)==16384
    with tempfile.TemporaryDirectory(prefix='large-content-') as directory:
        root=pathlib.Path(directory);p=root/'original';p.mkdir();data=p/'board';users=p/'users';mail=p/'users.mail'
        s=Server(binary,data,members=users);a,b,c=[Client(s.port) for _ in range(3)]
        try:
            for client,name in ((a,'alice'),(b,'bob'),(c,'carol')):auth(client,name,True)
            assert post(a,'short','short')==1
            assert post(a,'large',body,1)==2;assert read(a,2)==(1,'large',expected)
            command(a,'POST draft',b'EDIT');a.send((body+'\n').encode());assert preview(a)[2]==expected.encode()
            assert preview(a)[2]==expected.encode()
            command(a,'/SAVE',b'DRAFT STORED');command(a,'/CANCEL',b'CANCELLED')
            assert send(a,'bob','large-mail',body)==1;assert expected.replace("\r\n","\n").rstrip("\n").encode() in mail_read(b,1)
            command(a,'MAIL SEND bob saved-large',b'MAIL EDIT');a.send((body+'\n').encode());command(a,'/SAVE',b'MAIL DRAFT SAVED')
            command(a,'MAIL RESUME',b'MAIL DRAFT');assert preview(a)[2]==expected.encode();command(a,'/CANCEL',b'CANCELLED')
            assert send(a,'bob','erase-this','deleted-secret-payload')==2
            command(b,'MAIL REPLY 2',b'MAIL EDIT');b.send(b'kept reply\n.\n');b.expect(b'MAIL SENT 3')
            command(b,'MAIL REPLY 2',b'MAIL EDIT');b.send(b'stale reply draft\n');command(b,'/SAVE',b'MAIL DRAFT SAVED')
            command(a,'MAIL DELETE 2',b'MAIL DELETE CONFIRM');command(a,'Y',b'MAIL DELETED 2')
            for kind,path in (('board',data),('mail',mail)):
                compact(kind,path,root/(kind+'-busy'),False);assert not (root/(kind+'-busy')).exists()
            command(a,'MAIL 0',b'MAIL CLOSED');command(a,'POST blank-lines',b'EDIT');a.send(b'\n'*8192)
            assert len(lines(a)[1])==8192
            command(a,'/CANCEL',b'CANCELLED')
        finally:a.close();b.close();c.close();s.close()
        # Runtime must reject truncated extended records without repairing them.
        for path in (data,users,mail):
            original=path.read_bytes();path.write_bytes(original[:-1])
            rejected=subprocess.run([binary,'0',str(data),str(users)],capture_output=True,timeout=10)
            assert rejected.returncode and path.read_bytes()==original[:-1]
            path.write_bytes(original)
        before={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in p.iterdir()}
        for kind,path in (('board',data),('mail',mail)):
            report=compact(kind,path,root/(kind+'.new'))
            assert report['saved_bytes']>0 and report['source_sha256']==before[path.name]
            assert report['output_sha256']==hashlib.sha256((root/(kind+'.new')).read_bytes()).hexdigest()
            assert compact(kind,path)['output_sha256']==report['output_sha256']
            assert compact(kind,root/(kind+'.new'))['output_sha256']==report['output_sha256']
            compact(kind,path,root/(kind+'.new'),False)
            damaged=root/(kind+'.bad');damaged.write_bytes(path.read_bytes()[:-1]);compact(kind,damaged,root/(kind+'.failed'),False)
            assert not (root/(kind+'.failed')).exists()
        assert b'deleted-secret-payload' not in (root/'mail.new').read_bytes()
        assert before=={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in p.iterdir()}
        q=root/'compacted';shutil.copytree(p,q);shutil.copyfile(root/'board.new',q/'board');shutil.copyfile(root/'mail.new',q/'users.mail')
        s=Server(binary,q/'board',members=q/'users');a,b,c=[Client(s.port) for _ in range(3)]
        try:
            for client,name in ((a,'alice'),(b,'bob'),(c,'carol')):auth(client,name)
            assert read(a,2)==(1,'large',expected)
            assert expected.replace("\r\n","\n").rstrip("\n").encode() in mail_read(b,1);assert b'MAIL ERROR unavailable'==mail_read(c,1)
            assert b'MAIL ERROR unavailable'==mail_read(b,2);assert b'REPLY 2' in mail_read(a,3)
            command(b,'MAIL RESUME',b'MAIL ERROR unavailable')
            command(a,'MAIL RESUME',b'MAIL DRAFT');assert preview(a)[2]==expected.encode();command(a,'/CANCEL',b'CANCELLED');command(a,'MAIL 0',b'MAIL CLOSED')
            command(a,'RESUME',b'DRAFT RESUMED');assert preview(a)[2]==expected.encode();command(a,'/CANCEL',b'CANCELLED')
            assert post(a,'after compact','more',2)==3;assert send(a,'bob','after compact','more')==4
        finally:a.close();b.close();c.close();s.close()
        s=Server(binary,q/'board',members=q/'users',menu=True,telnet=True,language='en');a=Telnet(s.port)
        try:
            a.until();a.cmd('LOGIN alice',b'> ');a.cmd(PASSWORD);a.cmd('PAGER OFF')
            assert b'END-LARGE' in a.cmd('READ 2')
            assert b'END-LARGE' in a.cmd('MAIL READ 1',b'0 exit)> ');a.cmd('MAIL 0')
            a.cmd('PAGER ON');out,marker=screen(a,'READ 2');pages=0
            while marker==MORE:
                more,marker=screen(a,'');out+=more;pages+=1;assert pages<100
            assert pages>0 and b'END-LARGE' in out
            a.cmd('PAGER OFF');a.cmd('LANG ja');assert b'END-LARGE' in a.cmd('READ 2')
        finally:a.close();s.close()
        compact('board',q/'board');compact('mail',q/'users.mail')
        if os.name=='posix':
            import fault_io as resource
            import signal
            for kind,target in [('board','board'),('mail','users.mail'),('draft','users')]:
                limited=root/('limited-'+kind);shutil.copytree(p,limited)
                path=limited/target;limit=path.stat().st_size+100
                def constrain():
                    signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
                    resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
                s=Server(binary,limited/'board',members=limited/'users',preexec_fn=constrain,options=["--user-log","-"]);a=Client(s.port)
                try:
                    auth(a,'alice')
                    command(a,'MAIL SEND bob fail' if kind=='mail' else 'RESUME' if kind=='draft' else 'POST fail',b'MAIL EDIT' if kind=='mail' else b'DRAFT RESUMED' if kind=='draft' else b'EDIT')
                    if kind!='draft':a.send((body+'\n').encode())
                    command(a,'/SAVE' if kind=='draft' else '.',b'ERR member storage unavailable' if kind=='draft' else b'MAIL ERROR storage' if kind=='mail' else b'ERR storage uncertain')
                finally:a.close();s.close()
                damaged=path.read_bytes();assert len(damaged)==limit
                failed=subprocess.run([binary,'0',str(limited/'board'),str(limited/'users')],capture_output=True,timeout=10)
                assert failed.returncode and path.read_bytes()==damaged
        # Upgrade old small-board formats without changing their article IDs.
        legacy=root/'legacy';row=bytearray(1120);row[:16]=(1).to_bytes(4,'little')+bytes(4)+(1).to_bytes(4,'little')+(3).to_bytes(4,'little');row[16]=65;row[96:99]=b'x\r\n'
        sys.path.insert(0,str(tool.parent));from notes_compact import sealed
        legacy.write_bytes(b'RTBOARD1'+sealed(row));compact('board',legacy,root/'upgraded')
        s=Server(binary,root/'upgraded');a=Client(s.port)
        try:assert read(a,1)==(0,'A','x\r\n');assert post(a,'long',body)==2
        finally:a.close();s.close()
    print('PASS: 16 KiB article/mail/drafts, exact boundaries and 8192-line listing; raw/JP/EN Telnet paging; compact copy/restart/replies/privacy/deleted body removal; locks/collision/corruption/source invariance and legacy upgrade; POSIX partial-write rejection')

if __name__=='__main__':run(sys.argv[1])
