"""Bounded connector blocks preserve legacy limits and per-operation permissions."""
import asyncio,base64,hashlib,re,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from webui.bridge import Bridge
from test_board import Server

async def run(server):
    a=await Bridge('127.0.0.1',server.port).open();b=await Bridge('127.0.0.1',server.port).open();guest=await Bridge('127.0.0.1',server.port).open()
    enc=lambda data:base64.b64encode(data).decode()
    data=bytes(range(256))*256+b'x';digest=hashlib.sha256(data).hexdigest()
    try:
        assert 'ERR vault syntax' in (await guest.command(f'CONNECTOR BLOCK {guest.token} 1'))['text']
        await a.command('LOGIN alice');await a.command('block-test-password');assert a.state['admin']
        await b.command('REGISTER bob');await b.command('block-test-password')
        assert 'ERR vault syntax' in (await a.command(f'CONNECTOR BLOCK {a.token} 90001'))['text']
        assert 'ERR command' in (await b.command(f'CONNECTOR BLOCK {a.token} 1'))['text']
        await a.command(f'CONNECTOR BLOCK {a.token} 9')
        assert 'ERR vault syntax' in (await a.command(f'CONNECTOR DATA {a.token} LEVEL 2 5'))['text']
        await b.command();assert b.state['level']!=5
        start=await a.command(f'FILESTART {len(data)} {digest} ZmlsZS5iaW4= -');ident=int(re.search(r'^UPLOAD (\d+)',start['text'])[1])
        assert 'ERR vault denied' in (await b.command(f'FILEPUTBLOCK {ident} 0 '+enc(data[:65536])))['text']
        assert 'ERR vault syntax' in (await a.command(f'FILEPUT {ident} 0 '+enc(data[:673])))['text']
        assert 'ERR vault syntax' in (await a.command(f'FILEPUTBLOCK {ident} 0 '+enc(data)))['text']
        assert f'RECEIVED {ident} 65536' in (await a.command(f'FILEPUTBLOCK {ident} 0 '+enc(data[:65536])))['text']
        assert f'RECEIVED {ident} 65537' in (await a.command(f'FILEPUTBLOCK {ident} 65536 '+enc(data[-1:])))['text']
        assert 'UPLOADED' in (await a.command(f'FILEFINISH {ident}'))['text']
        opened=await b.command(f'FILEOPEN {ident}');ticket=opened['text'].split()[1]
        fetched=await b.command(f'FILEFETCHBLOCK {ticket} 0');match=re.search(r'DOWNLOADDATA 0 65536 (\S+)',fetched['text']);assert match and base64.b64decode(match[1])==data[:65536]
        assert 'ERR vault syntax' in (await b.command(f'FILEDONE {ticket} {digest}'))['text']
        await a.command(f'FILEHIDE {ident} CONFIRM')
        assert 'DOWNLOADDATA' not in (await b.command(f'FILEFETCHBLOCK {ticket} 65536'))['text']
        await b.command('FILEABORT '+ticket)
        start=await b.command(f'MFILESTART {len(data)} {digest} bWFpbC5iaW4= -');private=int(re.search(r'^UPLOAD (\d+)',start['text'])[1])
        await b.command(f'MFILEPUTBLOCK {private} 0 '+enc(data[:65536]));await b.command(f'MFILEPUTBLOCK {private} 65536 '+enc(data[-1:]));await b.command(f'MFILEFINISH {private}')
        denied=await a.command(f'MFILEGETBLOCK {private} 0');assert 'FILEDATA' not in denied['text'] and 'ERR' in denied['text'],denied
        assert 'PONG' in (await guest.command('PING'))['text']
        print('PASS connector blocks: size bounds, legacy line limit, token matching, guest refusal, command allowlist, owner checks, mid-download hiding, no premature receipt, private mail denies SYSOP')
    finally:await a.close();await b.close();await guest.close()

if __name__=='__main__':
    binary=str(Path(sys.argv[1]).resolve());ctl=str(Path(sys.argv[2]).resolve())
    with tempfile.TemporaryDirectory(prefix='ln-block-') as directory:
        root=Path(directory);subprocess.run([ctl,'init',str(root/'vault')],check=True,capture_output=True)
        bootstrap=Server(binary,root/'notes',members=root/'members',telnet=True)
        async def seed():
            b=await Bridge('127.0.0.1',bootstrap.port).open()
            try:await b.command('REGISTER alice');await b.command('block-test-password')
            finally:await b.close()
        try:asyncio.run(seed())
        finally:bootstrap.close()
        (root/'admins').write_text('1\n');server=Server(binary,root/'notes',members=root/'members',vault=root/'vault',telnet=True,admins=root/'admins')
        try:asyncio.run(run(server))
        finally:server.close()
