"""Durable private mail drafts, conflict handling and atomic send consumption."""
import pathlib,sys,tempfile,subprocess,os,signal
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_telnet import Client as Telnet
from test_talk import command
from test_mail import read,send

def info(c):
    c.send(b'MAIL DRAFT\n');r=c.line();assert r.startswith(b'MAIL DRAFT '),r
    return int(r.split()[2])

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='maildraft-') as d:
        p=pathlib.Path(d);data=p/'bbs';members=p/'members'
        s=Server(binary,data,members=members);a,b,c=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice',True);auth(b,'bob',True);auth(c,'alice')
            command(a,'POST article-draft',b'EDIT ');command(a,'/SAVE',b'DRAFT STORED');command(a,'/CANCEL',b'CANCELLED')
            command(a,'MAIL SEND bob subject',b'MAIL EDIT ')
            command(a,'/SAVE',b'MAIL DRAFT SAVED') # empty body allowed
            rev=info(a)
            command(b,'MAIL RESUME',b'MAIL ERROR unavailable')
            command(c,'MAIL SEND bob overwrite',b'MAIL EDIT ');command(c,'/SAVE',b'MAIL ERROR unavailable');command(c,'/CANCEL',b'CANCELLED')
            command(a,'MAIL RESUME',b'MAIL DRAFT ');a.send(b'private body\n');command(a,'/SAVE',b'MAIL DRAFT SAVED')
            assert info(a)>rev
        finally:a.close();b.close();c.close();s.close()
        s=Server(binary,data,members=members);a,b,c=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob');auth(c,'alice')
            command(a,'MAIL RESUME',b'MAIL DRAFT ');command(c,'MAIL RESUME',b'MAIL DRAFT ')
            command(a,'/TITLE new-subject',b'DRAFT TITLE CHANGED');command(a,'/SAVE',b'MAIL DRAFT SAVED')
            command(c,'/SAVE',b'MAIL ERROR unavailable');command(c,'.',b'MAIL ERROR unavailable');command(c,'/CANCEL',b'CANCELLED')
            command(a,'MAIL RESUME',b'MAIL DRAFT ');command(a,'.',b'MAIL SENT 1')
            assert b'new-subject' in read(b,1) and b'private body' in read(b,1)
            command(a,'MAIL RESUME',b'MAIL ERROR unavailable')
            command(a,'0',b'MAIL CLOSED');command(a,'RESUME',b'DRAFT RESUMED');command(a,'/CANCEL',b'CANCELLED') # article draft untouched
            command(b,'MAIL REPLY 1',b'MAIL EDIT ');b.send(b'reply body\n');command(b,'/SAVE',b'MAIL DRAFT SAVED');rev=info(b)
            command(b,'MAIL DRAFT DELETE '+str(rev+1),b'MAIL ERROR unavailable')
            command(a,'MAIL DELETE 1',b'MAIL DELETE CONFIRM');command(a,'Y',b'MAIL DELETED 1')
            command(b,'MAIL RESUME',b'MAIL ERROR unavailable');assert info(b)==rev
            command(b,'MAIL DRAFT DELETE '+str(rev),b'MAIL DRAFT DELETED')
        finally:a.close();b.close();c.close();s.close()
        s=Server(binary,data,members=members);a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice');auth(b,'bob');command(a,'MAIL RESUME',b'MAIL ERROR unavailable');command(b,'MAIL RESUME',b'MAIL ERROR unavailable')
        finally:a.close();b.close();s.close()
        for lang in ['en','ja']:
            s=Server(binary,data,members=members,menu=True,telnet=True,language=lang);a=Telnet(s.port)
            prompt=b'0 exit)> ' if lang=='en' else '0終了)> '.encode()
            bodyprompt=b'Body > ' if lang=='en' else '本文 > '.encode()
            try:
                a.until();a.cmd('LOGIN alice',b'> ');a.cmd(PASSWORD)
                a.cmd('MAIL SEND bob subject',bodyprompt);a.cmd('hello',bodyprompt)
                saved=a.cmd('/SAVE',prompt);assert (b'draft saved' if lang=='en' else '下書きを保存'.encode()) in saved
                a.cmd('MAIL RESUME',bodyprompt)
                helptext=a.cmd('/HELP',bodyprompt);assert b'MAIL RESUME' in helptext
                preview=a.cmd('/PREVIEW',bodyprompt);assert b'hello' in preview
                a.cmd('.',prompt)
            finally:a.close();s.close()
        raw=(p/'members.mail').read_bytes()
        if os.name=='posix':
            import fault_io as resource
            limit=len(raw)+100
            def size_limit():signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,data,members=members,preexec_fn=size_limit,options=["--user-log","-"]);a=Client(s.port)
            try:
                auth(a,'alice');command(a,'MAIL SEND bob failure',b'MAIL EDIT ');a.send(b'kept body\n');command(a,'/SAVE',b'MAIL ERROR storage')
                command(a,'/TITLE retained',b'DRAFT TITLE CHANGED');command(a,'/CANCEL',b'CANCELLED')
            finally:a.close();s.close()
            assert (p/'members.mail').stat().st_size==limit
            r=subprocess.run([binary,'0',str(data),str(members)],capture_output=True,timeout=10)
            assert r.returncode and (p/'members.mail').stat().st_size==limit
            (p/'members.mail').write_bytes(raw)
        (p/'members.mail').write_bytes(raw[:-1])
        r=subprocess.run([binary,'0',str(data),str(members)],capture_output=True,timeout=10)
        assert r.returncode and (p/'members.mail').read_bytes()==raw[:-1]
    print('PASS: mail draft empty/save/restart/resume, article isolation, owner isolation, concurrent stale-save/send rejection, atomic send removal, deleted reply target and revision-checked deletion')
if __name__=='__main__':run(sys.argv[1])
