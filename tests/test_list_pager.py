from preference_assertions import assert_preferences_only
import pathlib,sys,tempfile
from test_board import Server,post
from test_runtime import Client
from test_members import auth,PASSWORD
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient
from test_pager import command,MORE,JA

def screens(c,text):
    out,end=command(c,text); result=out.replace(MORE,b'').replace(JA,b''); count=1
    while end in (MORE,JA):
        out,end=command(c,''); result+=out.replace(MORE,b'').replace(JA,b''); count+=1
        assert count<150
    return result,count

def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='list-pager-') as d:
        p=pathlib.Path(d); board=p/'board'; users=p/'users'; admins=p/'admins'; config=p/'boards'
        config.write_text('1|0|all|member|Main\n'+''.join(f'{i}|1|all|member|Board-{i:02d}\n' for i in range(2,13)))
        s=Server(binary,board,members=users,catalog=config); a=Client(s.port)
        try:
            auth(a,'alice',True)
            for i in range(12): post(a,f'note-{i:02d} 日本語','body')
            for i in range(12): post(a,f'reply-{i:02d} 日本語','body',1)
            expect(a,'PAGER ON','PAGER ON'); expect(a,'PREFS SAVE','PREFS SAVED ja ON')
            for i in range(9):
                c=Client(s.port)
                try: auth(c,f'user{i}',True)
                finally: c.close()
        finally: a.close(); s.close()
        admins.write_text('1\n')
        s=Server(binary,board,members=users,catalog=config,admins=admins); a=Client(s.port)
        try:
            auth(a,'alice')
            for i in range(6): expect(a,'HIDE 24','HIDDEN SAVED 24'); expect(a,'RESTORE 24','RESTORED 24')
        finally: a.close(); s.close()
        snapshot={f:f.read_bytes() for f in [board,users,p/'board.moderation']}
        s=Server(binary,board,members=users,catalog=config,admins=admins,menu=True,language='en'); a,b=Menu(s.port),Menu(s.port)
        try:
            a.until(); b.until(); a.cmd('LOGIN alice','> '); a.cmd(PASSWORD)
            a.cmd('LANG en'); a.cmd('PAGER SIZE 80 7')
            for cmd,token,total in [('NOTES','note-',12),('TREE 1','reply-',12),('SEARCH 日本語','note-',12),('LIST','reply-',12),('UNREAD','reply-',12),('BOARDS','Board-',11),('MEMBERS','user',9),('AUDIT','Actor',12)]:
                out,n=screens(a,cmd); assert n>1,(cmd,n)
                allout=out
                while b'+: Next page' in out:
                    out,n=screens(a,'+'); allout+=out
                assert allout.count(token.encode())==total,(cmd,allout)
            assert 'note-04' in b.cmd('NOTES')
            command(a,'NOTES'); out,end=command(a,'+'); assert end==MORE
            assert 'PONG' in b.cmd('PING')
            command(a,'Q'); out,n=screens(a,'+'); assert b'note-05' in out
            # Selection after cancelling a screen still refers to an article, not a screen number.
            command(a,'SEARCH 日本語'); command(a,'Q'); out,n=screens(a,'13'); assert b'Article #13' in out
            out,n=screens(a,'TREE'); assert b'Reply tree' in out
            command(a,'NOTES'); out,end=command(a,'OFF'); assert end==b'] > ' and b'note-04' in out
            assert 'OFF' in a.cmd('PAGER'); assert 'note-04' in a.cmd('NOTES')
            a.cmd('PAGER ON'); a.cmd('LANG ja'); out,end=command(a,'BOARDS'); assert end==JA
            command(a,'Q'); a.cmd('RAW','RAW\r\n'); a.send(b'NOTES\n'); raw=a.until('END NEXT 6\r\n'); assert 'note-04' in raw and 'Enter/止' not in raw
            a.cmd('MENU'); a.cmd('PAGER OFF'); a.cmd('PAGER SIZE 20 5'); a.cmd('PAGER ON')
            out,n=screens(a,'SEARCH 日本語'); assert n>2
            assert '日本語' in out.decode().replace('\r\n','')
        finally: a.close(); b.close(); s.close()
        print('PASS: all eight list types, screen/result pagination, saved preference, cancel/select/tree, OFF, Japanese and raw bypass')
        s=Server(binary,board,members=users,catalog=config,admins=admins,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); a.cmd('PAGER ON'); a.send(bytes([255,250,31,0,80,0,7,255,240]))
            out,end=command(a,'NOTES'); assert end==MORE
            a.send(bytes([255,250,31,0,80,0,60,255,240]))
            out,end=command(a,''); assert end==b'] > ' and b'note-04' in out
            out,n=screens(a,'+'); assert b'note-05' in out
        finally: a.close(); s.close()
        for f,raw in snapshot.items():
            if f==users:assert_preferences_only(raw,f.read_bytes())
            else:assert f.read_bytes()==raw
        print('PASS: Telnet resize during list display, continuation cursor and byte-identical article/member/moderation files')

if __name__=='__main__': run(sys.argv[1])
