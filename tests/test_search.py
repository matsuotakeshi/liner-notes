import pathlib
import sys
import tempfile
import time
from test_runtime import Client
from test_board import Server, post, read
from test_members import auth, unread, PASSWORD
from test_notes import page
from test_moderation import expect
from test_ui import Menu
from test_telnet import Client as TelnetClient


def results(c,query):
    rows=[]; cursor=0
    while True:
        header,part,cursor=page(c,'SEARCH '+query if not cursor else f'SEARCHFROM {cursor} {query}')
        assert header==('SEARCH '+query).encode()
        rows+=part
        if not cursor: return rows


def ids(rows): return [int(r.split()[0]) for r in rows]


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='search-') as d:
        p=pathlib.Path(d); data=p/'data'; users=p/'users'; config=p/'boards'; admins=p/'admins'
        config.write_text('1|0|all|member|Public\n2|1|u2|u2|Private\n',encoding='utf-8')
        s=Server(binary,data,members=users); a,b=Client(s.port),Client(s.port)
        try: auth(a,'alice',True); auth(b,'bob',True)
        finally: a.close(); b.close(); s.close()
        s=Server(binary,data,members=users,catalog=config); a,b=Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'bob')
            assert results(a,'empty')==[]
            for i in range(12): post(a,f'日本語 title {i}','needle body' if i%2 else 'ordinary')
            assert post(a,'reply','needle body',1)==13
            expect(b,'BOARD 2','BOARD 2 Private'); assert post(b,'private needle','secret contents')==14
            for i in range(260): post(b,'private filler','no match')
            expect(b,'BOARD 1','BOARD 1 Public'); assert post(b,'last needle','tail')==275
        finally: a.close(); b.close(); s.close()
        admins.write_text('1\n')
        s=Server(binary,data,members=users,catalog=config,admins=admins); a,b,g=Client(s.port),Client(s.port),Client(s.port)
        try:
            auth(a,'alice'); auth(b,'bob'); expect(a,'HIDE 2','HIDDEN SAVED 2')
            before=unread(a)
            rows=results(a,'needle'); assert ids(rows)==[4,6,8,10,12,13,275]
            assert b'private' not in b'\n'.join(rows) and b'contents' not in b'\n'.join(rows)
            assert ids(results(g,'日本語'))==[1,3,4,5,6,7,8,9,10,11,12]
            assert results(a,'Needle')==[] and ids(results(a,'needle body'))==[4,6,8,10,12,13]
            assert ids(results(a,'12'))==[] and ids(results(a,'title 11'))==[12]
            for command,error in [('SEARCH','ERR search query'),('SEARCH '+('x'*81),'ERR search query'),('SEARCH a\tb','ERR search query'),('SEARCHFROM 0 needle','ERR search cursor'),('SEARCHFROM 10001 needle','ERR search cursor'),('SEARCHFROM 999999999 needle','ERR search cursor'),('SEARCHFROM 2','ERR search cursor')]: expect(a,command,error)
            expect(a,'BOARD 2','ERR board unavailable'); assert ids(results(a,'needle'))==[4,6,8,10,12,13,275]
            expect(b,'BOARD 2','BOARD 2 Private'); assert ids(results(b,'needle'))==[14]
            assert unread(a)==before
            # FTS skips nonmatches; ACL checks still filter each candidate.
            h,rows,cursor=page(g,'SEARCHFROM 15 needle'); assert ids(rows)==[275] and cursor==0
            start=time.monotonic(); expect(a,'PING','PONG'); assert time.monotonic()-start<1
            assert ids(page(g,'SEARCHFROM 271 needle')[1])==[275]
            # Visibility is checked anew on every page, and restore makes a match searchable again.
            expect(a,'RESTORE 2','RESTORED 2'); assert 2 in ids(results(g,'needle'))
            expect(a,'HIDE 2','HIDDEN SAVED 2'); assert 2 not in ids(results(a,'needle'))
        finally: a.close(); b.close(); g.close(); s.close()
        snapshot={f:f.read_bytes() for f in [data,users,p/'data.moderation']}
        print('PASS: title/body UTF-8 literal matching, replies, guest search, case sensitivity, board isolation, hidden exclusion and restore')
        print('PASS: bounded candidate scans, indexed gaps, invalid input, connection isolation and unchanged unread state')
        s=Server(binary,data,members=users,catalog=config,admins=admins,menu=True); a,b=Menu(s.port),Menu(s.port)
        try:
            a.until(); b.until(); assert '検索語' in a.cmd('SEARCH','> ')
            assert '取り消しました' in a.cmd('/CANCEL')
            a.cmd('SEARCH','> '); out=a.cmd('日本語'); assert '検索結果' in out and '次ページ' in out
            b.cmd('LANG en'); assert 'Search results' in b.cmd('SEARCH needle')
            a.cmd('LANG en'); out=a.cmd('+'); assert 'title 9' in out and 'Search results' in out
            assert 'Article #13' in a.cmd('13'); assert 'Reply tree' in a.cmd('TREE')
            assert 'Search results' in b.cmd('SEARCH needle')
        finally: a.close(); b.close(); s.close()
        s=Server(binary,data,members=users,catalog=config,admins=admins,menu=True,telnet=True,language='en'); a=TelnetClient(s.port)
        try:
            a.until(); assert b'Search text' in a.cmd('SEARCH',b'> ')
            out=a.cmd('needle');assert b'Search results' in out and b'last needle' in out
            assert b'Article #275' in a.cmd('275')
        finally: a.close(); s.close()
        for f,raw in snapshot.items(): assert f.read_bytes()==raw,f
        print('PASS: restart, Japanese/English menus, per-session query pagination, result-to-article/tree navigation, Telnet and byte-identical stores')


if __name__=='__main__': run(sys.argv[1])
