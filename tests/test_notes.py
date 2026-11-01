"""HyperNotes-style branching, navigation, paging and durable tree tests."""
import concurrent.futures
import pathlib
import struct
import sys
import tempfile
from test_runtime import Client
from test_board import Server, post, read


def page(c, command):
    c.send((command+'\n').encode())
    header=c.line(); rows=[]
    assert not header.startswith(b'ERR'), header
    while True:
        row=c.line()
        if row.startswith(b'END NEXT '): return header,rows,int(row.split()[-1])
        if row.startswith(b'FILEDETAIL '):
            assert rows and rows[-1].startswith(b'FILEITEM '),row
            assert rows[-1].split()[1]==row.split()[1],row
            continue  # Metadata supplements the preceding file, not a second item.
        rows.append(row)


def tree(c, root):
    cursor=0; rows=[]; seen=set()
    while True:
        h,r,cursor=page(c,f'TREE {root}'+(f' {cursor}' if cursor else ''))
        assert h==f'TREE {root}'.encode()
        rows+=r
        if not cursor: return rows
        assert cursor not in seen; seen.add(cursor)


def navigate(c, cmd, expected):
    c.send((cmd+'\n').encode())
    h=c.line()
    assert h.startswith(f'ARTICLE {expected} PARENT '.encode()),h
    c.line(); n=int(c.line().split()[-1])
    while len(c.buf)<n:
        data=c.sock.recv(8192); assert data; c.buf+=data
    c.buf=c.buf[n:]; c.expect(b'END')


def fixture(path, count):
    data=bytearray(b'RTBOARD1')
    for i in range(1,count+1):
        raw=struct.pack('<IIII',i,i-1,1,3)+b't'+bytes(79)+b'b\r\n'+bytes(1021)
        h=2166136261
        for b in raw: h=((h^b)*16777619)&0xffffffff
        data+=raw+struct.pack('<I',h)
    path.write_bytes(data)


def run(binary):
    binary=str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='notes-') as d:
        path=pathlib.Path(d)/'notes.dat'; s=Server(binary,path)
        a,b=Client(s.port),Client(s.port)
        try:
            a.send(b'PARENT\n'); a.expect(b'ERR no current article')
            assert post(a,'Base','root')==1
            assert post(a,'child A','A',1)==2
            assert post(a,'child B','B',1)==3
            assert post(a,'grandchild','deep',2)==4
            assert post(a,'Other','other')==5
            read(a,1); navigate(a,'CHILD',2); navigate(a,'NEXT',3); navigate(a,'PREV',2)
            navigate(a,'CHILD',4); navigate(a,'PARENT',2)
            a.send(b'PREV\n'); a.expect(b'ERR no neighbour')
            a.send(b'READ 999\n'); a.expect(b'ERR no article')
            navigate(a,'PARENT',1)  # errors did not change selection
            read(b,3)
            a.send(b'RESPOND selected root\n'); a.expect(b'EDIT end with . or /CANCEL')
            a.send(b'answer\n.\n'); a.expect(b'SAVED 6')
            navigate(b,'PARENT',1)  # selection is session-local
            assert read(a,6)[0]==1
            rows=tree(a,1)
            assert [int(r.strip().split()[0][1:]) for r in rows]==[1,2,4,3,6]
            assert rows[2].startswith(b'    #4 parent=2 depth=2')
            a.send(b'TREE 1 5\n'); a.expect(b'ERR tree cursor')
            a.send(b'TREE 1 10001\n'); a.expect(b'ERR tree cursor')
            print('PASS: branched DFS, parent/child/sibling navigation, current reply and session isolation')
            def add(i):
                c=Client(s.port)
                try: return post(c,f'parallel {i}','reply',1)
                finally: c.close()
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                ids=list(pool.map(add,range(12)))
            assert sorted(ids)==list(range(7,19))
            saved=tree(a,1); assert len(saved)==17
            assert len({r.strip().split()[0] for r in saved})==17
            _,notes,_=page(a,'NOTES')
            assert notes[0].startswith(b'#1 replies=16 latest=18 updated=') and b'unknown' not in notes[0]
            for i in range(6): post(a,f'root {i}','root')
            _,roots,cursor=page(a,'NOTES'); assert len(roots)==5 and cursor
            _,rest,last=page(a,f'NOTES {cursor}'); assert len(rest)==3 and last==0
            notes_before=page(a,'NOTES')
            print('PASS: concurrent same-parent replies, tree/notes pagination, reply totals and UTC timestamps')
        finally:
            a.close(); b.close(); s.close()
        s=Server(binary,path); a=Client(s.port)
        try:
            assert tree(a,1)==saved
            assert page(a,'NOTES')==notes_before
            read(a,4); navigate(a,'PARENT',2); navigate(a,'NEXT',3)
        finally: a.close(); s.close()
        assert path.read_bytes()[:8]==b'RTBOARD4'
        print('PASS: tree, ordering, navigation and timestamps restored after restart')
        old=pathlib.Path(d)/'old.dat'; fixture(old,10000); before=old.read_bytes()
        s=Server(binary,old); a=Client(s.port)
        try:
            _,rows,cursor=page(a,'TREE 1 9998')
            assert len(rows)==3 and cursor==0
            assert b'depth=9999' in rows[-1] and len(rows[-1])<160
            read(a,10000); navigate(a,'PARENT',9999)
            _,rows,_=page(a,'NOTES')
            assert b'replies=9999 latest=10000 updated=unknown' in rows[0]
        finally: a.close(); s.close()
        assert old.read_bytes()==before
        print('PASS: 10000-level legacy fixture loads without recursion, bounded indent, no data rewrite')


if __name__=='__main__': run(sys.argv[1])
