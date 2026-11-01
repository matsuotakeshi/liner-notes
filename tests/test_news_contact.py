"""Date/search privacy and authenticated connection contact controls."""
import pathlib,sys,tempfile,struct,datetime,time,os
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_talk import command
from test_mail import send,listing
from test_notes import page
from test_telnet import Client as Telnet

def roster(c):
    c.send(b'ONLINE\n');rows=[]
    while True:
        row=c.line()
        if row==b'CHAT END':return b'\n'.join(rows)
        rows.append(row)

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='news-contact-') as d:
        p=pathlib.Path(d);data=p/'bbs';users=p/'members';config=p/'boards';admins=p/'admins'
        s=Server(binary,data,members=users);a,b,c=[Client(s.port) for _ in range(3)]
        try:
            for x,name in [(a,'alice'),(b,'bob'),(c,'carol')]:auth(x,name,True)
            for i in range(12):post(a,'article-'+str(i),'body')
            for i in range(12):send(a,'bob','subject-'+str(i),'本文 needle '+str(i))
            send(a,'carol','private-title','private-body')
        finally:a.close();b.close();c.close();s.close()
        raw=bytearray(data.read_bytes());assert raw[:8]==b'RTBOARD4'
        for i in range(12):
            pos=8+1168*i;struct.pack_into('<Q',raw,pos+1120,int(datetime.datetime(2020,1,i+1,tzinfo=datetime.timezone.utc).timestamp()))
            h=2166136261
            for v in raw[pos:pos+1164]:h=((h^v)*16777619)&0xffffffff
            struct.pack_into('<I',raw,pos+1164,h)
        data.write_bytes(raw);admins.write_text('1\n');config.write_text('1|0|all|member|Public\n2|1|u2|u2|Secret\n')
        s=Server(binary,data,members=users,catalog=config,admins=admins);a,b,c,g=[Client(s.port) for _ in range(4)]
        try:
            for x,name in [(a,'alice'),(b,'bob'),(c,'carol')]:auth(x,name)
            command(a,'HIDE 2',b'HIDDEN SAVED 2');command(a,'WITHDRAW 3 CONFIRM',b'WITHDRAW SAVED 3')
            command(b,'BOARD 2',b'BOARD 2');post(b,'secret','secret')
            before=data.read_bytes() if os.name!='nt' else None;mail_before=(p/'members.mail').read_bytes() if os.name!='nt' else None
            header,rows,cursor=page(a,'NEWS ALL 2020-01-05');assert header==b'NEWS ALL 2020-01-05' and [int(r.split()[0]) for r in rows]==list(range(5,13))
            assert not page(a,f'NEWSFROM {cursor} ALL 2020-01-05')[1] if cursor else True
            assert not page(a,'NEWS 9999-12-31')[1]
            assert [int(r.split()[0]) for r in page(g,'NEWS 2020-01-01')[1]]==[1,4,5,6,7,8,9,10]
            for query in ['2020-02-30','1900-01-01','2023-02-29','2020-1-01','2020-01-01 x']:command(a,'NEWS '+query,b'ERR extended syntax')
            assert len([r for r in listing(b,'MAIL SEARCH needle') if r.startswith(b'MAIL ROW')])==8
            assert len([r for r in listing(b,'+') if r.startswith(b'MAIL ROW')])==4
            assert len([r for r in listing(b,'MAIL SEARCH 本文') if r.startswith(b'MAIL ROW')])==8
            assert not any(r.startswith(b'MAIL ROW') for r in listing(c,'MAIL SEARCH needle'))
            assert not any(r.startswith(b'MAIL ROW') for r in listing(b,'MAIL SEARCH private-body'))
            assert len([r for r in listing(a,'MAIL SEARCH subject-11') if r.startswith(b'MAIL ROW')])==1
            if os.name!='nt':assert data.read_bytes()==before and (p/'members.mail').read_bytes()==mail_before
            for x in (a,b,c):command(x,'MAIL 0',b'MAIL CLOSED')
            command(g,'MEMO no',b'ERR login required');command(g,'CALL no',b'ERR login required')
            command(b,'MEMO 昼休み',b'MEMO SAVED');assert '昼休み'.encode() in roster(a)
            command(b,'MEMO '+('x'*81),b'ERR command');command(b,'MEMO',b'MEMO SAVED');assert '昼休み'.encode() not in roster(a)
            command(b,'CALL hello',b'CALL DELIVERED 1');assert a.line().startswith(b'CHAT SYSOP CALL')
            command(b,'CALL again',b'CALL ERROR rate')
            command(a,'POST busy',b'EDIT');command(c,'CALL busy',b'CALL DELIVERED 0');command(a,'/CANCEL',b'CANCELLED')
            command(b,'MEMO temporary',b'MEMO SAVED');command(b,'LOGOUT',b'LOGGED OUT');assert b'temporary' not in roster(a)
        finally:a.close();b.close();c.close();g.close();s.close()
        s=Server(binary,data,members=users,catalog=config,admins=admins,menu=True,telnet=True,language='en');a,b=Telnet(s.port),Telnet(s.port)
        try:
            for x,name in [(a,'alice'),(b,'bob')]:x.until();x.cmd('LOGIN '+name,b'> ');x.cmd(PASSWORD);x.cmd('LANG en');x.cmd('PAGER OFF')
            assert b'local midnight' in a.cmd('NEWS ALL 2020-01-01');assert b'article-10' in a.cmd('+')
            assert b'article-10' in a.cmd('11')
            assert b'subject/body search' in b.cmd('MAIL SEARCH needle',b'0 exit)> ');assert b'subject-11' in b.cmd('+',b'0 exit)> ');b.cmd('0')
            assert b'Connection memo' in b.cmd('MEMO lunch');assert b'lunch' in a.cmd('ONLINE')
            b.cmd('LANG ja');assert '接続者メモ'.encode() in b.cmd('MEMO 休憩')
            assert '通知しました'.encode() in b.cmd('CALL test');assert b'SYSOP call from' in a.until()
        finally:a.close();b.close();s.close()
    print('PASS: UTC article dates, invalid dates, hidden/withdrawn/ACL exclusion, search pagination and non-mutating mail/body privacy; connection memos, guest denial, SYSOP-only calls, cooldown, busy rejection, JP/EN Telnet')
if __name__=='__main__':run(sys.argv[1])
