"""Retention preserves totals, whole connections, Gather state, backups and locks."""
import importlib.util,pathlib,tempfile,time,sys,subprocess,hashlib,json
from test_board import Server
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd,rows,counts
BASE=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('retention',BASE/'scripts/log-retain.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

def record(kind,actor,target,text,stamp):return dict(kind=kind,actor=actor,target=target,text=text,time=stamp)

def data(magic,records):
    for i,r in enumerate(records,1):r['seq']=i
    return m.encode(magic,records)

def refuses(fn):
    try:fn()
    except (ValueError,OSError):return
    raise AssertionError('expected refusal')

def run(binary,ctl):
    binary=str(pathlib.Path(binary).resolve());ctl=str(pathlib.Path(ctl).resolve());now=int(time.time());old=now-100*86400
    with tempfile.TemporaryDirectory(prefix='retain59-') as directory:
        p=pathlib.Path(directory);board=p/'notes';members=p/'members';admins=p/'admins';vault=p/'vault'
        s=Server(binary,board,members=members);c=Client(s.port);auth(c,'admin',True);c.close();c=Client(s.port);auth(c,'bob',True);c.close();s.close();admins.write_text('1\n')
        subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True);f=p/'payload';f.write_bytes(b'abc');subprocess.run([ctl,'import',str(vault),str(f),'file','description'],check=True,capture_output=True)
        user=pathlib.Path(str(members)+'.userlog')
        original=data(b'LNUSER01',[
            record('C',0,0,'RAW',old),record('L',2,1,'',old+1),record('T',2,1,'65000',old+66),record('E',2,1,'70000',old+71),
            record('C',0,0,'TELNET',now-20),record('L',2,5,'',now-19),record('E',2,5,'5000',now-14)])
        user.write_bytes(original);result=m.run(user,p/'retained-user',30,now=now);assert result['output_records']==4
        assert user.read_bytes()==original and (p/'retained-user/archive.dat').read_bytes()==original
        retained=(p/'retained-user/retained.dat').read_bytes();magic,rr=m.read_rows(retained)
        assert rr[0]['kind']=='S' and rr[0]['text'].startswith('1 70000 ')
        assert rr[-1]['target']==2
        m.run(p/'retained-user/retained.dat',p/'second',30,now=now);assert (p/'second/retained.dat').read_bytes()==retained
        # A session crossing the cutoff is retained whole; an unclosed session too.
        unclosed=data(b'LNUSER01',[record('C',0,0,'RAW',old),record('L',2,1,'',old+1),record('T',2,1,'65000',old+66)])
        magic,rr=m.read_rows(unclosed);assert len(m.retain(magic,rr,now-30*86400,16))==3
        # Clean login/logout within one connection, then another user logs in.
        multi=data(b'LNUSER01',[record('C',0,0,'RAW',old),record('L',2,1,'',old+1),record('O',2,1,'7000',old+8),record('L',1,1,'',old+9),record('E',1,1,'9000',old+18)])
        magic,rr=m.read_rows(multi);summaries=m.retain(magic,rr,now-30*86400,16);assert {r['actor']:r['text'].split()[:2] for r in summaries}=={1:['1','9000'],2:['1','7000']}
        downloads=vault/'downloads.dat';drecords=[record('D',0,1,f'legacy-member:2 {i:032x} 3',old+i) for i in range(1,13)]
        downloads.write_bytes(data(b'LNDOWN01',drecords));m.run(downloads,p/'retained-download',30,now=now)
        dr=(p/'retained-download/retained.dat').read_bytes();assert len(dr)==4104
        # Safe explicit test-copy installation; real source remains in its archive.
        user.write_bytes(retained);downloads.write_bytes(dr)
        s=Server(binary,board,members=members,admins=admins,vault=vault);a=Client(s.port)
        try:
            auth(a,'admin');assert counts(a)==12
            assert b'LOGINS 2 SECONDS 75' in rows(a,'USERSTATS 2',b'END USERSTATS')[0]
            assert b' S ' not in b'\n'.join(rows(a,'USERLOG ALL'))
            assert b'DOWNLOADROW' not in b'\n'.join(rows(a,'FILEHISTORY'))
            assert cmd(a,'FILEDONE '+('1'.zfill(32))+' '+hashlib.sha256(b'abc').hexdigest())==b'ERR download ticket'
            assert counts(a)==12
            # The running runtime owns the same exclusive log locks.
            refuses(lambda:m.run(user,p/'locked',30));assert not (p/'locked').exists()
            refuses(lambda:m.run(downloads,p/'locked-down',30));assert not (p/'locked-down').exists()
        finally:a.close();s.close()
        # An old unclosed session must not overwrite a newer summarized last login.
        overlapping=data(b'LNUSER01',[record('C',0,0,'RAW',old-1000),record('L',2,1,'',old-999),record('C',0,0,'RAW',old),record('L',2,3,'',old+1),record('E',2,3,'5000',old+6)])
        user.write_bytes(overlapping);m.run(user,p/'overlapping',30,now=now);user.write_bytes((p/'overlapping/retained.dat').read_bytes())
        s=Server(binary,board,members=members,admins=admins,vault=vault);a=Client(s.port)
        try:
            auth(a,'admin');assert f'LOGINS 2 SECONDS 5 LAST {old+1}'.encode() in rows(a,'USERSTATS 2',b'END USERSTATS')[0]
        finally:a.close();s.close()
        chat=p/'chat';chat.write_bytes(data(b'LNCHAT01',[record('S',2,0,'bob\tBob\told text',old),record('S',2,0,'bob\tBob\trecent text',now)]))
        m.run(chat,p/'retained-chat',30,now=now);assert len((p/'retained-chat/retained.dat').read_bytes())==4104
        activity=p/'activity';activity.write_bytes(data(b'LNACTV01',[record('G',2,0,f'message {i}\r\n',old+i) for i in range(20)]+[record('A',1,0,'old notice',old)]))
        m.run(activity,p/'retained-activity',30,gather_count=16,now=now);_,r=m.read_rows((p/'retained-activity/retained.dat').read_bytes());assert len(r)==16 and r[0]['text']=='message 4\r\n'
        unchanged=chat.read_bytes();m.run(chat,None,0,check=True);assert chat.read_bytes()==unchanged
        refuses(lambda:m.run(chat,p/'retained-chat',30))
        broken=p/'broken';broken.write_bytes(unchanged[:-1]);refuses(lambda:m.run(broken,p/'bad-output',30));assert not (p/'bad-output').exists()
        state=p/'state';state.write_bytes(b'LNCAT001');refuses(lambda:m.run(state,p/'bad-state',30))
        invalid=p/'invalid';invalid.write_bytes(data(b'LNCHAT01',[record('E',2,12,'0',old)]).replace(b'LNCHAT01',b'LNUSER01',1));refuses(lambda:m.run(invalid,p/'bad-user',30))
    print('PASS retention: stable totals/replay, whole sessions, receipt rejection, backup integrity, Gather, lock/corruption/state refusal')

if __name__=='__main__':run(*sys.argv[1:])
