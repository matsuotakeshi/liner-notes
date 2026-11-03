"""Real Telnet transport, opt-in synchronization, literal bodies and core permissions."""
import asyncio
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from webui.bridge import Bridge, response_data
from test_board import Server


async def exercise(port):
    a=await Bridge('127.0.0.1',port).open()
    b=await Bridge('127.0.0.1',port).open()
    try:
        assert a.state['version']==1 and not a.state['user']
        r=await a.command('REGISTER alice');assert r['state']['secret']
        r=await a.command('web-test-password');assert r['state']['user']==1 and 'web-test-password' not in r['text']
        r=await b.command('REGISTER bob');assert r['state']['secret']
        r=await b.command('web-test-password');assert r['state']['user']==2
        assert 'ERR admin required' in (await b.command('LEVEL 1 5'))['text']
        r=await a.command('POST Web connector');assert r['state']['editing']
        body=['/CANCEL','.','CONNECTOR STATE not-a-marker {}','あ'*1365+'x']
        async with a.lock:
            for line in body:
                r=await a.literal(line);assert r['state']['editing'] and 'ERR ' not in r['text'],r
        r=await a.command('.');assert 'SAVED 1' in r['text'],r
        r=await b.command('READ 1');article=response_data(r['text']).get('article')
        assert article and article['body']=='\r\n'.join(body)+'\r\n',r
        r=await a.command('MAIL SEND bob private');assert r['state']['editing']
        async with a.lock:await a.literal('private body')
        r=await a.command('.');assert 'MAIL SENT 1' in r['text'],r
        r=await b.command('MAIL READ 1');assert response_data(r['text'])['message']['body']=='private body\r\n',r
        await b.command('MAIL EXIT')
        r=await b.command('CHAT');assert r['state']['chat'],r
        r=await b.command('ordinary chat text');assert 'ordinary chat text' in r['text'],r
        assert 'CONNECTOR SYNC' not in r['text']
        r=await b.command('/LEAVE');assert not r['state']['chat'],r
        r=await b.command('PASSWD');assert r['state']['secret'],r
        r=await b.command('/CANCEL');assert not r['state']['secret'],r
        print('PASS Web connector: real Telnet, password secrecy, member ACL, literal 4096-byte lines, article/mail bodies and chat synchronization')
    finally:await a.close();await b.close()


if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='ln-web-test-') as tmp:
        p=Path(tmp);server=Server(str(Path(sys.argv[1]).resolve()),p/'notes',members=p/'members',menu=True,telnet=True)
        try:asyncio.run(exercise(server.port))
        finally:server.close()
