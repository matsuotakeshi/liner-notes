"""Private mail boundaries, durable read/delete/reply and interactive entry."""
import pathlib,sys,tempfile,subprocess,struct,os,signal
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_talk import command
from test_telnet import Client as Telnet

def listing(c,cmd='MAIL'):
    c.send(cmd.encode()+b'\n');rows=[]
    while True:
        line=c.line();rows.append(line)
        if line.startswith(b'MAIL END NEXT '):return rows

def send(c,target,title,body):
    command(c,'MAIL SEND '+target+' '+title,b'MAIL EDIT ')
    c.send(body.encode()+b'\n.\n');return int(c.line().split()[-1])

def read(c,n):
    c.send(('MAIL READ '+str(n)+'\n').encode());rows=[]
    while True:
        line=c.line();rows.append(line)
        if line==b'MAIL END' or line.startswith(b'MAIL ERROR'):return b'\n'.join(rows)

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='mail-') as d:
        p=pathlib.Path(d);data=p/'bbs';members=p/'members';admins=p/'admins'
        s=Server(binary,data,members=members);clients=[Client(s.port) for _ in range(4)];a,b,c,g=clients
        try:
            for x,name in [(a,'alice'),(b,'bob'),(c,'carol')]:auth(x,name,True)
            command(g,'MAIL',b'ERR login required')
            n=send(a,'bob','秘密の題名','秘密の本文');assert n==1
            assert b'READ 0' in read(a,n)
            assert not any(b'MAIL ROW' in row for row in listing(c))
            assert read(c,n)==b'MAIL ERROR unavailable'
            command(c,'MAIL REPLY 1',b'MAIL ERROR unavailable')
            command(c,'MAIL DELETE 1',b'MAIL DELETE CONFIRM');command(c,'Y',b'MAIL ERROR unavailable')
            rows=listing(b);assert b'UNREAD 1' in rows[0]
            assert '秘密の本文'.encode() in read(b,n);assert b'READ 1' in read(a,n)
            command(a,'MAIL REPLY 1',b'MAIL ERROR unavailable')
            command(b,'MAIL REPLY 1',b'MAIL EDIT ');b.send('返信内容\n.\n'.encode());b.expect(b'MAIL SENT 2')
            assert b'REPLY 1' in read(a,2)
            command(a,'MAIL DELETE 2',b'MAIL DELETE CONFIRM');command(a,'N',b'CANCELLED');assert b'MAIL ITEM 2' in read(a,2)
            command(b,'MAIL DELETE 2',b'MAIL DELETE CONFIRM');command(b,'Y',b'MAIL DELETED 2');assert b'ERROR unavailable' in read(a,2)
            command(a,'MAIL SEND missing nope',b'MAIL ERROR unavailable')
            command(a,'MAIL SEND bob no-leak',b'MAIL EDIT ')
            command(a,'/CANCEL',b'CANCELLED');command(a,'0',b'MAIL CLOSED')
            assert post(a,'public','public body')==1
            for i in range(10):send(a,'bob','batch'+str(i),'body')
            assert len([r for r in listing(b) if r.startswith(b'MAIL ROW')])==8
            assert len([r for r in listing(b,'+') if r.startswith(b'MAIL ROW')])==3
        finally:
            for x in clients:x.close()
            s.close()
        admins.write_text('3\n');s=Server(binary,data,members=members,admins=admins);b,c=Client(s.port),Client(s.port)
        try:
            auth(b,'bob');auth(c,'carol');assert b'UNREAD 10' in listing(b)[0]
            assert read(c,1)==b'MAIL ERROR unavailable'
            assert b'ERROR unavailable' in read(b,2)
        finally:b.close();c.close();s.close()
        journal=p/'members.mail';raw=journal.read_bytes()
        bad=bytearray(raw);bad[8+20:8+24]=struct.pack('<I',999);h=2166136261
        for v in bad[8:8+1148]:h=((h^v)*16777619)&0xffffffff
        struct.pack_into('<I',bad,8+1148,h)
        for damaged in [bad,raw[:-1]]:
            journal.write_bytes(damaged);r=subprocess.run([binary,'0',str(data),str(members)],capture_output=True,timeout=10)
            assert r.returncode and journal.read_bytes()==damaged
        journal.write_bytes(raw)
        if os.name=='posix':
            import fault_io as resource
            limit=len(raw)+100
            def size_limit():signal.signal(signal.SIGXFSZ,signal.SIG_IGN);resource.setrlimit(resource.RLIMIT_FSIZE,(limit,limit))
            s=Server(binary,data,members=members,preexec_fn=size_limit,options=["--user-log","-"]);a=Client(s.port)
            try:
                auth(a,'alice');command(a,'MAIL SEND bob fail',b'MAIL EDIT ');a.send(b'failed-body\n.\n');a.expect(b'MAIL ERROR storage')
            finally:a.close();s.close()
            assert journal.stat().st_size==limit;journal.write_bytes(raw)
        s=Server(binary,data,members=members,menu=True,telnet=True,language='en');a=Telnet(s.port)
        try:
            a.until();a.cmd('LOGIN alice',b'> ');assert b'Unread mail:' in a.cmd(PASSWORD)
            a.cmd('MAIL',b'0 exit)> ');a.cmd('E',b'abort)> ');a.cmd('bob',b'abort)> ');a.cmd('interactive',b'> ')
            a.cmd('hello',b'> ');assert b'saved and sent' in a.cmd('.',b'0 exit)> ')
            a.cmd('LANG ja','0終了)> '.encode());assert 'メール一覧'.encode() in a.cmd('L','0終了)> '.encode());a.cmd('0')
        finally:a.close();s.close()
    print('PASS: private mail send/read/recipient-only read flag/reply/delete confirmation, no third-party or admin access, paging, restart, malformed storage and bilingual interactive mail')

if __name__=='__main__':run(sys.argv[1])
