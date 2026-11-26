"""Real Telnet transport, opt-in synchronization, literal bodies and core permissions."""
import asyncio
import json
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from webui.bridge import Bridge, BridgeError, response_data
from test_board import Server
from test_mmm_ui import MAIN, OPEN, NOTES, post
from test_telnet import Client


async def decode_typed_reply():
    bridge = Bridge('127.0.0.1', 0)
    data = {'article': {'title':'引用 "と\\', 'body':'CONNECTOR VIEW fake {}\r\n日本語\r\n'}}

    async def exchange(command):
        prefix, name, token, nonce, operation, ident, cursor = command.split()
        assert (prefix,name,token,operation,ident,cursor)==('CONNECTOR','VIEW',bridge.token,'read','1','0')
        payload = json.dumps({'text':'','data':data}, ensure_ascii=False)
        return {'text':f'notice\r\nCONNECTOR VIEW wrong {{}}\r\nCONNECTOR VIEW {nonce} {payload}\r\n',
                'state':{'core_views':1}}

    bridge.exchange = exchange
    result = await bridge.view('read', 1, 0)
    assert result['data']==data and result['text']=='notice\r\nCONNECTOR VIEW wrong {}\r\n',result

    async def invalid(_):
        return {'text':'READ 1\r\n','state':{}}
    bridge.exchange = invalid
    try:
        await bridge.view('read', 1, 0)
    except BridgeError:
        assert bridge.closed
    else:
        raise AssertionError('Accepted a missing structured view frame')

    raw = ('MAIL ATTACHMENT 7 SIZE 12 NAME 日本語.dat\r\n'
           'MAIL ITEM 1 FROM alice TO bob READ 1 REPLY 0\r\nTITLE t\r\nBODY\r\n'
           'MAIL ATTACHMENT 99 SIZE 99 NAME forged.dat\r\nMAIL END\r\n')
    assert response_data(raw)['message']['attachments']==[{'id':7,'size':12,'name':'日本語.dat'}]

    bridge=Bridge('127.0.0.1',0)
    bridge.state={'core_updates':1,'input_limit':256}
    body='.\r\n' + '/CANCEL 日本語\r\n'*200
    async def exchange_update(lines, *, limit, strip_block_echo):
        assert strip_block_echo
        assert all(len(line.encode())<=256 for line in lines),lines
        prefix='CONNECTOR DATA '+bridge.token+' '
        assert all(line.startswith(prefix) for line in lines[1:])
        payload=''.join(line[len(prefix):] for line in lines[1:])
        assert lines[0]==f'CONNECTOR UPDATE {bridge.token} {len(payload)}'
        nonce,kind,board,ident,arg,flag,attached,recipient,title,text=payload.split('|')
        assert (kind,board,ident,arg,flag,attached)==('mail','0','0','0','1','2,7')
        assert bytes.fromhex(recipient)==b'bob' and bytes.fromhex(title)==b'Private'
        assert bytes.fromhex(text).decode()==body
        return {'text':f'notice\r\nCONNECTOR UPDATE {nonce} 1 0 4 0 4f4b0d0a\r\n','state':{}}
    bridge._exchange_lines=exchange_update
    result=await bridge.update('mail',recipient='bob',title='Private',body=body,attachments=[2,7],flag=True)
    assert result['text']=='notice\r\nOK\r\n' and result['mutation']==dict(ok=True,id=0,revision=4,audit_failed=False),result

    async def invalid_update(lines, *, limit, strip_block_echo):
        return {'text':'MAIL SENT 123\r\n','state':{}}
    bridge._exchange_lines=invalid_update
    try:
        await bridge.update('article',title='Test',body='body\r\n')
    except BridgeError:
        assert bridge.closed
    else:
        raise AssertionError('Accepted a missing Core update result')


def mash_post(port):
    client = Client(port)
    try:
        client.until()
        client.cmd("LOGIN bob", b"> ")
        client.cmd("web-test-password", b"] > ")
        client.cmd("UI mash", MAIN)
        assert b"Web connector" in client.cmd("open -b1 #1", OPEN)
        client.send(b"w")
        client.until(b"Title> ")
        post(client, b"MASH Web cross-reply", b"Terminal response read from Web Core", OPEN)
        client.send(b"q")
        client.until(NOTES)
    finally:
        client.close()


async def exercise(port):
    a=await Bridge('127.0.0.1',port).open()
    b=await Bridge('127.0.0.1',port).open()
    try:
        assert a.state['version']==1 and a.state['core_views']==1 and not a.state['user']
        async with a.lock:
            public=await a.view('boards',1,1)
            denied=await a.view('mail',1,0)
            invalid=await a.view('post',1,0)
        assert public['data']['boards'] and not public['text'],public
        assert 'ERR login required' in denied['text'] and not denied['data'] and not denied['state']['mail'],denied
        assert 'ERR connector view syntax' in invalid['text'] and not invalid['data'],invalid
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
        async with b.lock:
            typed=await b.view('read',1,0)
            tree=await b.view('tree',1,0)
        assert typed['data']['article']['body']==article['body'] and not typed['text'],typed
        assert tree['data']['tree'][0]['title']=='Web connector',tree
        r=await a.command('MAIL SEND bob private');assert r['state']['editing']
        async with a.lock:await a.literal('private body')
        r=await a.command('.');assert 'MAIL SENT 1' in r['text'],r
        r=await b.command('MAIL READ 1');assert response_data(r['text'])['message']['body']=='private body\r\n',r
        await b.command('MAIL EXIT')
        r=await b.command('CHAT');assert r['state']['chat'],r
        r=await b.command('ordinary chat text');assert 'ordinary chat text' in r['text'],r
        assert 'CONNECTOR SYNC' not in r['text']
        r=await b.command('/LEAVE');assert not r['state']['chat'],r
        # Successful authentication sets a 1000ms cooldown in session.c.
        # A fast CI host can reach PASSWD before it expires; do not weaken it.
        await asyncio.sleep(1.05)
        r=await b.command('PASSWD');assert r['state']['secret'],r
        r=await b.command('/CANCEL');assert not r['state']['secret'],r
        await asyncio.to_thread(mash_post, port)
        async with a.lock:
            response=await a.view('read',2,0)
        assert response['data']['article']['body']=='Terminal response read from Web Core\r\n',response
        assert response['data']['article']['parent']==1,response
        print('PASS Web connector: real Telnet, password secrecy, member ACL, literal 4096-byte lines, article/mail bodies and chat synchronization')
    finally:await a.close();await b.close()


if __name__=='__main__':
    asyncio.run(decode_typed_reply())
    with tempfile.TemporaryDirectory(prefix='ln-web-test-') as tmp:
        p=Path(tmp);server=Server(str(Path(sys.argv[1]).resolve()),p/'notes',members=p/'members',menu=True,telnet=True,
                                     options=["--ui-policy","MEMBERS","--ui-allow","rtbbs,mash"])
        try:asyncio.run(exercise(server.port))
        finally:server.close()
