from preference_assertions import assert_preferences_only
"""History filters apply to actors, preserve ACLs and survive page navigation."""
import pathlib
import struct
import sys
import tempfile
from test_board import Server, post
from test_runtime import Client
from test_members import auth, PASSWORD
from test_moderation import expect, checksum
from test_audit import history
from test_ui import Menu
from test_telnet import Client as TelnetClient
from test_list_pager import screens


def run(binary):
    binary = str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='audit-filters-') as d:
        p = pathlib.Path(d); data, users, admins, config = [p/n for n in ('data','users','admins','config')]
        s = Server(binary, data, members=users)
        a, b, c = [Client(s.port) for _ in range(3)]
        try:
            for client, name in [(a,'alice'),(b,'bob'),(c,'carol')]: auth(client,name,True)
            assert post(b,'public','body') == 1
        finally:
            a.close(); b.close(); c.close(); s.close()
        admins.write_text('1\n3\n'); config.write_text('1|0|all|member|Public\n2|0|u3|u3|Private\n')
        args = dict(members=users,admins=admins,catalog=config)
        s = Server(binary,data,**args)
        a,b,c,g = [Client(s.port) for _ in range(4)]
        try:
            for client,name in [(a,'alice'),(b,'bob'),(c,'carol')]: auth(client,name)
            for i in range(12): expect(b,f'RETITLE 1 revision-{i}','RETITLED 1')
            expect(a,'HIDE 1','HIDDEN SAVED 1'); expect(a,'RESTORE 1','RESTORED 1')
            expect(c,'BOARD 2','BOARD 2 Private'); assert post(c,'private','secret') == 2
            expect(c,'RETITLE 2 private-new','RETITLED 2')
            expect(c,'WITHDRAW 2 CONFIRM','WITHDRAW SAVED 2')
            expect(b,'AUDIT ARTICLE 1 ACTOR bob','ERR admin required')
            expect(a,'SUSPEND 2','SUSPENDED 2'); expect(a,'RESUME 2','RESUMED 2')
            expect(g,'AUDIT ACTOR unknown','ERR admin required')
            rows,cursor = history(a,'AUDIT ARTICLE 1 ACTOR bob')
            assert [e[0] for e in rows] == list(range(12,4,-1)) and cursor == 4
            assert all(e[2] == 2 and e[4] == 'RETITLE' and e[5] == 1 for e in rows)
            assert history(a,'AUDIT ACTORID 2 ARTICLE 1') == (rows,cursor)
            assert [e[0] for e in history(a,'AUDIT 4 ACTOR bob ARTICLE 1')[0]] == [4,3,2,1]
            assert [e[0] for e in history(a,'AUDIT ARTICLE 1 ACTOR alice')[0]] == [14,13]
            assert [e[0] for e in history(a,'AUDIT ACTOR alice')[0]] == [18,17,14,13]
            assert history(a,'AUDIT ARTICLE 2') == ([],0)
            assert history(a,'AUDIT ARTICLE 9999') == ([],0)
            expect(a,'AUDIT ACTOR unknown','ERR audit filter')
            expect(a,'AUDIT ACTORID 999','ERR audit filter')
            for cmd in ['AUDIT ARTICLE 0','AUDIT ARTICLE 2147483647','AUDIT ACTOR','AUDIT ACTORID 0','AUDIT ACTORID 1000000','AUDIT ACTOR bob ACTORID 2','AUDIT ARTICLE 1 ARTICLE 2','AUDIT 4294967296 ARTICLE 1','AUDIT ALL ARTICLE 1','AUDIT ARTICLE 1 extra']:
                expect(a,cmd,'ERR audit cursor')
            assert history(a,'AUDIT ALL') == history(a)
            saved = history(a,'AUDIT ARTICLE 1 ACTOR bob')
        finally:
            a.close(); b.close(); c.close(); g.close(); s.close()
        snapshot = {f:f.read_bytes() for f in (data,users,p/'data.extras',p/'data.moderation')}
        s = Server(binary,data,**args,menu=True)
        a,b = Menu(s.port),Menu(s.port)
        try:
            for client in (a,b):
                client.until(); client.cmd('LOGIN alice','> '); client.cmd(PASSWORD)
            view = a.cmd('AUDIT ARTICLE 1 ACTOR bob')
            assert '絞り込み: 記事 #1' in view and '操作者ID 2' in view and '#12 ' in view
            assert '#4 ' in a.cmd('+') and '#1 ' in a.cmd('1')
            assert '#12 ' in a.cmd('AUDIT REFRESH') and '#18 ' not in a.cmd('AUDIT REFRESH')
            assert '#18 ' in b.cmd('AUDIT REFRESH')  # Filters are session-local.
            assert '見つかりません' in a.cmd('AUDIT ACTOR unknown')
            assert '#12 ' in a.cmd('AUDIT REFRESH')  # Rejected query preserves the previous view.
            a.cmd('LANG en'); a.cmd('PAGER SIZE 80 8'); a.cmd('PAGER ON')
            out,count = screens(a,'AUDIT REFRESH')
            assert count>1 and b'Filter: Article #1' in out and b'Actor ID 2' in out
            a.cmd('PAGER OFF'); assert '#4 ' in a.cmd('+')
            assert '#18 ' in a.cmd('AUDIT ALL')
            a.cmd('RAW','RAW\r\n'); assert history(a,'AUDIT ARTICLE 1 ACTOR bob') == saved
        finally:
            a.close(); b.close(); s.close()
        s = Server(binary,data,**args,menu=True,telnet=True,language='en')
        a = TelnetClient(s.port)
        try:
            a.until(); a.cmd('LOGIN alice',b'> '); a.cmd(PASSWORD)
            assert b'Filter: Actor ID 2' in a.cmd('AUDIT ACTOR bob')
            assert b'#4 ' in a.cmd('+') and b'#12 ' in a.cmd('audit refresh')
        finally:
            a.close(); s.close()
        assert all(f.read_bytes()==original for f,original in snapshot.items() if f!=users)
        assert_preferences_only(snapshot[users],users.read_bytes())
        print('PASS: combined article/actor filters, actor vs target, ACLs, invalid queries, session isolation, paging, languages, Telnet and restart')

        # A full page scan with no matching actor must still return a continuation.
        fixture = bytearray(b'RTMOD001')
        for seq in range(1,301):
            row=bytearray(32); row[0]=ord('M'); row[1]=seq%2
            struct.pack_into('<IIIIQ',row,4,seq,1,1,2 if seq==1 else 1,1700000000+seq)
            row[28:]=checksum(row); fixture+=row
        (p/'data.moderation').write_bytes(fixture)
        s=Server(binary,data,**args); a=Client(s.port)
        try:
            auth(a,'alice'); rows,cursor=history(a,'AUDIT ACTOR bob')
            assert rows==[] and cursor==58
            collected=[]
            while cursor:
                rows,next_id=history(a,f'AUDIT {cursor} ACTOR bob')
                assert next_id<cursor
                collected+=rows; cursor=next_id
            assert len(collected)==13 and all(e[2]==2 for e in collected)
        finally:
            a.close(); s.close()
        print('PASS: bounded scan returns an empty page with a usable filtered continuation')


if __name__ == '__main__':
    run(sys.argv[1])
