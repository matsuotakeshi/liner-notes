"""HTTP integration against an isolated real BBS. No production data is used."""
import asyncio
import base64
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import aiohttp
from aiohttp import web
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from webui.app import create_app
from webui.bridge import Bridge
from test_board import Server

ROOT=Path(__file__).resolve().parents[1]

async def exercise(bbs,tmp):
    sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
    origin=f'http://127.0.0.1:{port}';base=origin+'/notes/api/'
    config={'web':{'listen':'127.0.0.1','origin':origin,'secure_cookies':False,'spool':str(tmp/'spool'),'messages':str(ROOT/'webui/messages')},'bbs':{'host':'127.0.0.1','port':bbs.port,'telnet':True}}
    app=create_app(config);runner=web.AppRunner(app);await runner.setup();await web.TCPSite(runner,'127.0.0.1',port).start()
    clients=[]
    async def client(name):
        c=aiohttp.ClientSession(cookie_jar=aiohttp.CookieJar(unsafe=True),headers={'Origin':origin});clients.append(c)
        r=await c.post(base+'session',json={});assert r.status==200,await r.text();v=await r.json();c.headers['X-CSRF-Token']=v['csrf']
        r=await c.post(base+'auth',json={'login':name,'password':'web-test-password','register':name!='alice'});assert r.status==200,await r.text();v=await r.json();assert v['state']['user'],v;c.headers['X-CSRF-Token']=v['csrf']
        return c
    async def post(c,path,payload):
        r=await c.post(base+path,json=payload);assert r.status==200,(r.status,await r.text());return await r.json()
    try:
        a=await client('alice');b=await client('bob');outsider=await client('carol')
        info=await post(a,'query',{'kind':'boards'});assert info['state']['games']==[{'id':1,'title':'Web test game'}],info
        invalid=await a.post(base+'query',json=[]);assert invalid.status==400
        for path in ('-','games','games/child','other'):
            r=await post(a,'input',{'line':f'FILEBOARDSET {path}|member|member'});assert 'FILEBOARD UPDATED' in r['text'],r
        r=await post(a,'input',{'line':'FILEBOARDSIGOP games|2'});assert 'FILEBOARD UPDATED' in r['text'],r
        r=await post(b,'query',{'kind':'files','path':'games/child'});assert r['state']['vault_sigop'] and not r['state']['admin'] and not r['state']['sigop'],r
        assert any(x['id']=='admin.file_name' for x in r['actions']),r
        r=await post(b,'action',{'action':'admin.file_name','values':{'path':'games/child','text':'Child files'}});assert 'FILEBOARD UPDATED' in r['text'],r
        r=await post(b,'action',{'action':'admin.file_name','values':{'path':'other','text':'Forbidden'}});assert 'ERR vault denied' in r['text'],r
        r=await post(b,'query',{'kind':'files','path':'other'});assert not r['state']['vault_sigop'],r
        r=await post(outsider,'query',{'kind':'files','path':'games/child'});assert not r['state']['vault_sigop'],r
        r=await a.post(base+'input',json={'line':'PING'},headers={'Origin':'https://attacker.invalid'});assert r.status==403
        r=await a.post(base+'input',json={'line':'PING'},headers={'X-CSRF-Token':'bad'});assert r.status==403
        r=await outsider.post(base+'action',json={'action':'admin.level','values':{'user':'1','level':'5'},'confirmed':True});assert r.status==409
        content='/CANCEL\n.\n<script>alert(1)</script>\n日本語の本文'
        r=await post(a,'compose',{'kind':'article','title':'Web article','body':content});assert 'SAVED 1' in r['text'],r
        r=await post(b,'query',{'kind':'notes'});assert r['data']['notes'][0]['title']=='Web article',r
        r=await post(b,'query',{'kind':'read','id':1});assert r['data']['article']['body']==content.replace('\n','\r\n')+'\r\n',r
        await post(a,'action',{'action':'settings.editor','values':{'value':'LINE'}})
        r=await post(a,'compose',{'kind':'article','title':'Large draft','body':'\n'.join(['x'*4094]*64),'draft':True});assert 'DRAFT STORED' in r['text'] and not r['state']['editing'],r['text']
        r=await post(a,'action',{'action':'board.resume','values':{}});assert len(r['preview'])>262144 and r['state']['editing'],r
        r=await post(a,'compose',{'kind':'editing','title':'Resumed','body':'line editor /CANCEL\n.'});assert 'SAVED 2' in r['text'],r
        r=await post(a,'gather',{'body':'Web Gather\n日本語の伝言'});assert 'GATHER SAVED' in r['text'],r
        r=await post(b,'gather',{});assert '日本語の伝言' in r['text'],r
        payload=bytes(range(256))*13+b'\x00\xff';digest=hashlib.sha256(payload).hexdigest();enc=lambda x:base64.b64encode(x.encode()).decode()
        headers={'X-File-Name':enc('日本語.bin'),'X-File-Description':enc('Web test'),'X-File-Board':'-'}
        r=await a.post(base+'upload',data=payload,headers=headers);assert r.status==200,await r.text();v=await r.json();fid=v['id'];assert v['sha256']==digest
        r=await post(a,'query',{'kind':'files'});assert r['data']['files'][0]['name']=='日本語.bin',r
        r=await b.post(base+'download',json={'id':fid});assert r.status==200,await r.text();assert await r.read()==payload
        r=await post(a,'download-ticket',{'id':fid,'name':'日本語.bin'});url=origin+r['url']
        denied=await outsider.get(url);assert denied.status==404
        dl=await a.get(url);assert dl.status==200 and 'filename*=UTF-8' in dl.headers['Content-Disposition'];assert await dl.read()==payload
        again=await a.get(url);assert again.status==404
        r=await a.post(base+'upload?kind=mail',data=payload,headers=headers);assert r.status==200,await r.text();attachment=(await r.json())['id']
        r=await outsider.post(base+'download',json={'kind':'mail','id':attachment});assert r.status==409,await r.text()
        r=await post(a,'compose',{'kind':'mail','title':'private','recipient':'bob','body':'私信本文','attachments':[attachment]});assert 'MAIL SENT 1' in r['text'],r
        r=await post(b,'query',{'kind':'mail'});assert r['data']['mail'][0]['title']=='private',r
        r=await post(b,'query',{'kind':'mail_read','id':1});assert r['data']['message']['body']=='私信本文\r\n',r
        r=await b.post(base+'download',json={'kind':'mail','id':attachment});assert r.status==200,await r.text();assert await r.read()==payload
        r=await outsider.post(base+'download',json={'kind':'mail','id':attachment});assert r.status==409,await r.text()
        # Resume a partially written core upload; HTTP must validate the original bytes.
        raw=await post(a,'input',{'line':f'FILESTART {len(payload)} {digest} cmVzdW1lLmJpbg== -'})
        ident=int(raw['text'].split()[1]);await post(a,'input',{'line':f'FILEPUT {ident} 0 '+base64.b64encode(payload[:123]).decode()})
        r=await a.post(base+'upload',data=b'changed',headers={**headers,'X-Upload-Resume':str(ident)});assert r.status==409
        r=await a.post(base+'upload',data=payload,headers={**headers,'X-Upload-Resume':str(ident)});assert r.status==200,await r.text();assert (await r.json())['id']==ident
        r=await a.post(base+'upload',data=payload,headers={**headers,'X-Upload-Resume':str(ident)});assert r.status==200;assert (await r.json())['already_committed']
        # WebSocket Origin and CSRF are checked before connecting a terminal.
        try:await a.ws_connect(base+'terminal',headers={'Origin':'https://attacker.invalid'},protocols=['liner-notes-v1','csrf.'+a.headers['X-CSRF-Token']])
        except aiohttp.WSServerHandshakeError as e:assert e.status==403
        else:raise AssertionError('Cross-origin terminal allowed')
        ws=await a.ws_connect(base+'terminal',protocols=['liner-notes-v1','csrf.'+a.headers['X-CSRF-Token']])
        async def terminal_until(marker):
            data=b''
            while marker not in data:
                msg=await ws.receive(timeout=10);assert msg.type==aiohttp.WSMsgType.BINARY,msg;data+=msg.data
            return data
        await terminal_until(b'RTBBS-PROBE')
        await ws.send_bytes(b'RAW\r\n');await terminal_until(b'RAW\r\n')
        await ws.send_bytes(b'LOGIN alice\r\n');await terminal_until(b'PASSWORD')
        await ws.send_bytes(b'web-test-password\r\n');reply=await terminal_until(b'LOGGED IN alice');assert b'web-test-password' not in reply
        await ws.send_bytes(b'DOOR 1\r\n');await terminal_until(b'WEB DOOR READY')
        await ws.send_bytes(b'hello\r\n');await terminal_until(b'Back to BBS')
        await ws.send_bytes(b'PING\r\n');await terminal_until(b'PONG');await ws.close()
        print('PASS Web HTTP: CSRF/Origin, session isolation, 256KiB drafts, Gather, literal posts, mail/privacy, public/private files with SHA256, one-use downloads, resume/retry, terminal login and real PTY door return')
    finally:
        await asyncio.gather(*(c.close() for c in clients));await runner.cleanup()

if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='ln-http-test-') as directory:
        p=Path(directory);subprocess.run([str(Path(sys.argv[2]).resolve()),'init',str(p/'vault')],check=True,capture_output=True)
        game=p/'door';game.write_text('#!/bin/sh\nprintf "WEB DOOR READY\\n"\nIFS= read -r answer\nprintf "WEB DOOR DONE\\n"\n');game.chmod(0o700)
        conf=p/'doors';conf.write_text(f'1|0|15|{game}|{p}|Web test game\n')
        admins=p/'admins';admins.write_text('1\n')
        bootstrap=Server(str(Path(sys.argv[1]).resolve()),p/'notes',members=p/'members',telnet=True)
        async def seed():
            bridge=await Bridge('127.0.0.1',bootstrap.port).open()
            try:
                await bridge.command('REGISTER alice')
                r=await bridge.command('web-test-password');assert r['state']['user']==1,r
            finally:await bridge.close()
        try:asyncio.run(seed())
        finally:bootstrap.close()
        bbs=Server(str(Path(sys.argv[1]).resolve()),p/'notes',members=p/'members',admins=admins,menu=True,telnet=True,vault=p/'vault',language='en',options=['--doors',str(conf)])
        try:asyncio.run(exercise(bbs,p))
        finally:bbs.close()
