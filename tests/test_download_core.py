"""Download services over RAW sockets, legacy files and authoritative SQLite."""
from contextlib import closing, ExitStack
from pathlib import Path
import base64
import hashlib
import subprocess
import sys
import tempfile
from test_board import Server
from test_runtime import Client
from test_members import auth
from test_stage58 import cmd, rows


def expect(c, text, expected):
    got=cmd(c,text);assert got==expected,(text,got,expected)


def opened(c):
    result=cmd(c,'FILEOPEN 1').split()
    assert result[0]==b'DOWNLOAD',result
    return result[1].decode(),int(result[2]),result[3].decode()


def fetch(c,token,offset):
    result=cmd(c,f'FILEFETCH {token} {offset}').split()
    assert result[0]==b'DOWNLOADDATA' and int(result[1])==offset,result
    data=b'' if result[3]==b'-' else base64.b64decode(result[3],validate=True)
    assert len(data)==int(result[2]);return data


def complete(c,payload):
    token,size,digest=opened(c);assert size==len(payload)
    data=bytearray()
    while len(data)<size: data.extend(fetch(c,token,len(data)))
    assert data==payload and hashlib.sha256(data).hexdigest()==digest
    expect(c,f'FILEDONE {token} {digest}',b'DOWNLOADED 1')
    return token,digest


def count(c):
    result=rows(c,'FILESTATS 1')
    return int(next(x for x in result if x.startswith(b'DOWNLOADCOUNT 1 ')).split()[2])


def run(binary,ctl,sqlite):
    with tempfile.TemporaryDirectory(prefix='download-core-') as directory:
        root=Path(directory);vault=root/'vault';admins=root/'admins'
        payload=bytes(range(256))*8+b'last';source=root/'payload.bin';source.write_bytes(payload)
        subprocess.run([ctl,'init',str(vault)],check=True,capture_output=True)
        subprocess.run([ctl,'import',str(vault),str(source),'payload.bin','download fixture'],check=True,capture_output=True)
        def start():
            return Server(binary,root/'notes',members=root/'members',vault=vault,language='en',
                          admins=admins if admins.exists() else None,
                          options=['--database',str(root/'authoritative.sqlite')] if sqlite else [])
        with closing(start()) as server, closing(Client(server.port)) as a, closing(Client(server.port)) as b:
            auth(a,'alice',True);auth(b,'bob',True)
        admins.write_text('1\n',encoding='ascii')
        with closing(start()) as server, ExitStack() as stack:
            a=stack.enter_context(closing(Client(server.port)));b=stack.enter_context(closing(Client(server.port)))
            auth(a,'alice');auth(b,'bob')
            expect(b,'FILEHISTORY',b'ERR vault denied')
            token,size,digest=opened(b)
            expect(a,f'FILEFETCH {token} 0',b'ERR download ticket')
            expect(b,f'FILEFETCH {token} 1',b'ERR vault syntax')
            expect(b,f'FILEDONE {token} {digest}',b'ERR vault syntax')
            assert fetch(b,token,0)==payload[:768]
            expect(a,'FILEBOARDSET -|none|none',b'FILEBOARD UPDATED')
            # vault_info conceals files outside the current board ACL as NOT_FOUND.
            expect(b,f'FILEFETCH {token} 768',b'ERR no file')
            expect(a,'FILEBOARDSET -|member|member',b'FILEBOARD UPDATED')
            expect(b,f'FILEABORT {token}',b'DOWNLOAD ABORTED')
            expect(b,f'FILEABORT {token}',b'DOWNLOAD ABORTED')
            assert count(a)==0
            token,digest=complete(b,payload)
            expect(b,f'FILEDONE {token} {digest}',b'DOWNLOADED 1');assert count(a)==1
        with closing(start()) as server, closing(Client(server.port)) as a, closing(Client(server.port)) as b:
            auth(a,'alice');auth(b,'bob')
            expect(b,f'FILEDONE {token} {digest}',b'DOWNLOADED 1');assert count(a)==1
            # Multiple pages before applying a per-board retention limit.
            for _ in range(9): complete(b,payload)
            first=rows(a,'FILEHISTORY 1');cursor=int(first[-1].split()[-1])
            assert cursor and sum(x.startswith(b'DOWNLOADROW ') for x in first)==8
            rest=rows(a,f'FILEHISTORY 1 {cursor}')
            assert sum(x.startswith(b'DOWNLOADROW ') for x in rest)==2
            assert all(b' legacy-member:2 ' in x and x.endswith(b' LINE') for x in first+rest if x.startswith(b'DOWNLOADROW '))
            expect(a,'FILEBOARDOPTIONS -|BOARD 0 2',b'FILEBOARD UPDATED')
            assert count(a)==10
            assert sum(x.startswith(b'DOWNLOADROW ') for x in rows(a,'FILEHISTORY 1'))==2
            # Pruned receipts no longer serve as duplicate-acknowledgement tokens.
            expect(b,f'FILEDONE {token} {digest}',b'ERR download ticket')
        with closing(start()) as server, closing(Client(server.port)) as a:
            auth(a,'alice');assert count(a)==10
            assert sum(x.startswith(b'DOWNLOADROW ') for x in rows(a,'FILEHISTORY 1'))==2
    print('PASS: download sockets, ACL changes, abort/completion/replay, history continuation/retention/restart; '+('SQLite' if sqlite else 'journals'))


if __name__=='__main__':
    binary,ctl=map(lambda s:str(Path(s).resolve()),sys.argv[1:3])
    for sqlite in (False,True): run(binary,ctl,sqlite)
