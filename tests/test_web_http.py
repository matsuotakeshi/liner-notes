"""HTTP integration against an isolated real BBS. No production data is used."""
import asyncio
import base64
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import aiohttp
from aiohttp import web
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
if os.environ.get('LINER_TEST_WEB_ROOT'):sys.path.insert(0,os.environ['LINER_TEST_WEB_ROOT'])
from webui.app import create_app
from webui.bridge import Bridge
from test_board import Server

ROOT=Path(__file__).resolve().parents[1]

async def exercise(bbs,tmp):
    sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
    origin=f'http://127.0.0.1:{port}';base=origin+'/notes/api/'
    config={'web':{'listen':'127.0.0.1','origin':origin,'secure_cookies':False,'spool':str(tmp/'spool'),'messages':str(ROOT/'webui/messages')},'bbs':{'host':'127.0.0.1','port':bbs.port,'telnet':True}}
    controls=[]
    async def control(reader,writer):
        controls.append(json.loads(await reader.readline()))
        writer.write(json.dumps(dict(node='a'*64,events=1,pending=0,deferred=0,bytes=4096,peers=[],scans=[],retry_reasons=[])).encode()+b'\n');await writer.drain();writer.close();await writer.wait_closed()
    config['web']['link_control_socket']=str(tmp/'control.sock')
    controller=await asyncio.start_unix_server(control,str(tmp/'control.sock'))
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
        async with b.get(base+'link') as r:assert r.status==403
        assert not controls
        async with a.get(base+'link') as r:assert r.status==200;assert (await r.json())['pending']==0
        async with a.post(base+'link',json={'action':'pause','peer':'b'*64},headers={'X-CSRF-Token':'wrong'}) as r:assert r.status==403
        assert len(controls)==1
        async with a.post(base+'link',json={'action':'pause','peer':'b'*64}) as r:assert r.status==200
        assert controls[-1]==dict(action='pause',peer='b'*64)
        async with a.post(base+'link',json={'action':'shell','peer':'b'*64}) as r:assert r.status==400
        for name in ('app.js','reports-ui.js','terminal-ui.js'):
            asset=await a.get(origin+'/notes/assets/'+name);assert asset.status==200,name
        r=await post(a,'action',{'action':'admin.connections','values':{}})
        assert r['data']['report']['title']=='CONNECTIONS' and len(r['data']['report']['rows'])>=3,r
        r=await post(a,'action',{'action':'admin.members','values':{}})
        assert r['data']['report']['title']=='MEMBERS' and len(r['data']['report']['rows'])==3,r
        r=await post(b,'action',{'action':'userstats','values':{}})
        assert r['data']['report']['title']=='USERSTATS',r
        assert not any(x['id']=='admin.connections' for x in r['actions']),r
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
        r=await post(a,'compose',{'kind':'article','title':'Web article','body':content});assert r['mutation']==dict(ok=True,id=1,revision=0,audit_failed=False),r
        for action,values in (
            ('board.search',{'scope':'HERE','query':'日本語の本文'}),
            ('board.author',{'scope':'SUBTREE','author':'alice'}),
        ):
            r=await post(b,'action',{'action':action,'values':values,'search_board':1})
            assert r['state']['core_search']==1 and not r['text'],r
            assert r['data']['matches']==[{'id':'1','board':'1','parent':'0','title':'Web article'}],r
        r=await post(b,'action',{'action':'board.news','values':{'date':'2000-01-01'},'search_board':1})
        assert r['state']['core_discovery']==1 and not r['text'],r
        assert [row['id'] for row in r['data']['matches']]==['1'],r
        r=await post(b,'action',{'action':'profile.search','values':{'query':'alice'}})
        assert not r['text'] and [(row['id'],row['login']) for row in r['data']['profiles']]==[(1,'alice')],r
        r=await post(a,'action',{'action':'profile.handle','values':{'text':'Web Search Handle'}})
        assert r['mutation']['ok'],r
        r=await post(b,'action',{'action':'profile.find','values':{'field':'handle','query':'Web Search Handle'}})
        assert not r['text'] and [row['login'] for row in r['data']['profiles']]==['alice'],r
        r=await post(a,'action',{'action':'profile.group','values':{'group':'100'}})
        assert not r['text'] and not r['data']['profiles'],r
        bad=await b.post(base+'action',json={'action':'board.search','values':{'query':'Web'},'cursor':True})
        assert bad.status==400,await bad.text()
        r=await post(b,'query',{'kind':'notes'});assert r['data']['notes'][0]['title']=='Web article',r
        r=await post(b,'action',{'action':'board.unread'})
        assert r['state']['core_operations']==1 and [row['id'] for row in r['data']['matches']]==['1'],r
        r=await post(b,'action',{'action':'board.check','values':{'scope':'HERE'}})
        assert r['data']['check']=={'active':True,'finished':True} and r['data']['matches'][0]['id']=='1',r
        r=await post(b,'action',{'action':'check.cancel'})
        assert r['data']['check']=={'active':False,'finished':False},r
        r=await post(b,'action',{'action':'check.done'})
        assert r['text'].startswith('ERR ') and not r['data']['check']['active'],r
        r=await post(b,'action',{'action':'board.check','values':{'scope':'HERE'}})
        assert r['data']['matches'][0]['id']=='1',r
        r=await post(b,'action',{'action':'check.done'})
        assert r['text'].startswith('CHECKPOINT SAVED') and not r['data']['check']['active'],r
        r=await post(b,'query',{'kind':'read','id':1});assert r['data']['article']['body']==content.replace('\n','\r\n')+'\r\n',r
        await post(a,'action',{'action':'settings.editor','values':{'value':'LINE'}})
        r=await post(a,'compose',{'kind':'article','title':'Large draft','body':'\n'.join(['x'*4094]*64),'draft':True});assert r['mutation']['ok'] and r['mutation']['revision'] and not r['state']['editing'],r['text']
        r=await post(a,'action',{'action':'board.resume','values':{}});assert len(r['preview'])>262144 and r['state']['editing'],r
        r=await post(a,'compose',{'kind':'editing','title':'Invalid','body':'\x85'});assert not r['mutation']['ok'] and r['state']['editing'],r
        preserved=await post(a,'input',{'line':'/PREVIEW'});assert 'Large draft' in preserved['text'] and 'x'*4094 in preserved['text'],preserved['text'][:150]
        r=await post(a,'compose',{'kind':'editing','title':'Resumed','body':'line editor /CANCEL\n.'});assert r['mutation']['ok'] and r['mutation']['id']==2,r
        r=await post(b,'compose',{'kind':'article','board':1,'parent':1,'title':'Reply','body':'literal /CANCEL'});assert r['mutation']['id']==3,r
        r=await post(b,'action',{'action':'board.retitle','values':{'id':'3','text':'Retitled'}});assert r['mutation']['ok'],r
        r=await post(outsider,'action',{'action':'board.withdraw','values':{'id':'3'},'confirmed':True});assert not r['mutation']['ok'],r
        r=await post(b,'action',{'action':'board.seen','values':{'id':'3'}});assert r['mutation']['ok'],r
        r=await post(b,'query',{'kind':'read','id':3})
        assert r['data']['article']['id']==3,r
        r=await post(b,'action',{'action':'board.parent'})
        assert not r['text'] and r['data']['article']['id']==1,r
        scan={'action':'board.scan','values':{'order':'DESC','scope':'HERE','author':'*','query':'*'},'search_board':1}
        r=await post(b,'action',scan)
        assert r['data']['article']['id']==3 and r['data']['next']==2,r
        r=await post(b,'action',{**scan,'cursor':r['data']['next']})
        assert r['data']['article']['id']==2,r
        r=await post(a,'gather',{'body':'Web Gather\n日本語の伝言'});assert r['mutation']['ok'] and 'GATHER SAVED' in r['text'],r
        r=await post(b,'gather',{})
        assert not r['text'] and r['data']['gather'][0]['body']=='Web Gather\r\n日本語の伝言\r\n',r
        payload=bytes(range(256))*13+b'\x00\xff';digest=hashlib.sha256(payload).hexdigest();enc=lambda x:base64.b64encode(x.encode()).decode()
        headers={'X-File-Name':enc('日本語.bin'),'X-File-Description':enc('Web test'),'X-File-Board':'-'}
        r=await a.post(base+'upload',data=payload,headers=headers);assert r.status==200,await r.text();v=await r.json();fid=v['id'];assert v['sha256']==digest
        r=await post(a,'query',{'kind':'files'});assert r['data']['files'][0]['name']=='日本語.bin',r
        for action,values in (
            ('file.list',{}),('file.subtree',{'path':'-'}),
            ('file.search',{'query':'*.bin'}),('file.search_in',{'path':'-','query':'*.bin'}),
            ('file.news',{'path':'-','date':'2000-01-01'}),
        ):
            r=await post(b,'action',{'action':action,'values':values})
            assert not r['text'] and [row['id'] for row in r['data']['files']]==[fid],r
        r=await post(b,'action',{'action':'file.search_in','values':{'path':'games/child','query':'*.bin'}})
        assert not r['text'] and not r['data']['files'],r
        r=await post(b,'action',{'action':'file.boards'})
        assert not r['text'] and any(row['path']=='games/child' and row['title']=='Child files' for row in r['data']['file_boards']),r
        r=await post(b,'action',{'action':'file.news','values':{'path':'-','date':'2001-02-29'}})
        assert r['text'].startswith('ERR ') and not r['data']['files'],r
        r=await post(b,'action',{'action':'file.detail','values':{'id':str(fid)}})
        assert not r['text'] and dict(r['data']['report']['fields'])['sha256']==digest,r
        r=await post(b,'action',{'action':'file.text','values':{'id':str(fid)}})
        assert r['text'].startswith('ERR ') and 'file_text' not in r['data'],r
        r=await post(b,'action',{'action':'file.children','values':{'path':'games'}})
        assert not r['text'] and r['data']['file_boards']==[{'path':'games/child','title':'Child files'}],r
        r=await post(a,'action',{'action':'file.desc','values':{'id':str(fid),'text':'更新した説明'}});assert r['mutation']['ok'],r
        r=await post(a,'action',{'action':'file.audit'})
        assert not r['text'] and r['data']['report']['title']=='FILEAUDIT',r
        r=await post(outsider,'action',{'action':'file.desc','values':{'id':str(fid),'text':'Unauthorized'}});assert not r['mutation']['ok'],r
        r=await b.post(base+'download',json={'id':fid});assert r.status==200,await r.text();assert await r.read()==payload
        r=await post(a,'action',{'action':'file.history'})
        assert not r['text'] and any(row[1]==str(fid) and row[4]==str(len(payload)) for row in r['data']['report']['rows']),r
        r=await post(a,'action',{'action':'file.stats'})
        assert not r['text'] and [str(fid),'1'] in r['data']['report']['rows'],r
        r=await post(a,'download-ticket',{'id':fid,'name':'日本語.bin'});url=origin+r['url']
        denied=await outsider.get(url);assert denied.status==404
        dl=await a.get(url);assert dl.status==200 and 'filename*=UTF-8' in dl.headers['Content-Disposition'];assert await dl.read()==payload
        again=await a.get(url);assert again.status==404
        r=await a.post(base+'upload?kind=mail',data=payload,headers=headers);assert r.status==200,await r.text();attachment=(await r.json())['id']
        for client,expected in ((a,[str(attachment)]),(b,[])):
            r=await post(client,'action',{'action':'mail.attachments'})
            assert not r['text'] and [row[0] for row in r['data']['report']['rows']]==expected,r
        r=await outsider.post(base+'download',json={'kind':'mail','id':attachment});assert r.status==409,await r.text()
        r=await post(outsider,'compose',{'kind':'mail','title':'Forbidden','recipient':'bob','body':'private','attachments':[attachment]});assert not r['mutation']['ok'],r
        r=await post(a,'compose',{'kind':'mail','title':'private','recipient':'bob','body':'私信本文','attachments':[attachment]});assert r['mutation']['ok'] and r['mutation']['id']==1,r
        r=await post(b,'query',{'kind':'mail'});assert r['data']['mail'][0]['title']=='private',r
        filters=(('mail.inbox',{}),('mail.unread',{}),('mail.news',{'date':'2000-01-01'}))
        for action,values in filters:
            r=await post(b,'action',{'action':action,'values':values})
            assert not r['text'] and [row['id'] for row in r['data']['mail']]==['1'],r
        r=await post(a,'action',{'action':'mail.sent'})
        assert not r['text'] and [row['id'] for row in r['data']['mail']]==['1'],r
        r=await post(outsider,'action',{'action':'mail.news','values':{'date':'2000-01-01'}})
        assert not r['text'] and not r['data']['mail'],r
        search={'action':'mail.search','values':{'query':'私信本文'}}
        r=await post(b,'action',search)
        assert not r['text'] and [m['id'] for m in r['data']['mail']]==['1'],r
        r=await post(outsider,'action',search)
        assert not r['text'] and not r['data']['mail'],r
        r=await post(b,'query',{'kind':'mail_read','id':1});assert r['data']['message']['body']=='私信本文\r\n',r
        assert r['data']['message']['attachments']==[{'id':attachment,'size':len(payload),'name':'日本語.bin'}],r
        denied=await post(outsider,'query',{'kind':'mail_read','id':1})
        assert 'message' not in denied['data'] and '私信本文' not in denied['text'],denied
        r=await post(outsider,'compose',{'kind':'reply_mail','parent':1,'title':'Forbidden','body':'reply'});assert not r['mutation']['ok'],r
        r=await post(b,'compose',{'kind':'reply_mail','parent':1,'title':'Reply draft','body':'返事','draft':True});assert r['mutation']['ok'] and r['mutation']['revision'],r
        r=await post(b,'action',{'action':'mail.resume','values':{}});assert r['state']['editing'] and 'Reply draft' in r['preview'],r
        r=await post(b,'compose',{'kind':'editing','title':'Reply sent','body':'返事の本文'});assert r['mutation']['ok'] and r['mutation']['id']==2 and not r['state']['editing'],r
        r=await post(a,'query',{'kind':'mail_read','id':2});assert r['data']['message']['parent']==1 and r['data']['message']['body']=='返事の本文\r\n',r
        r=await post(a,'action',{'action':'mail.prev'})
        assert not r['text'] and r['data']['message']['id']==1,r
        r=await post(outsider,'action',{'action':'mail.delete','values':{'id':'2'},'confirmed':True});assert not r['mutation']['ok'],r
        r=await post(a,'action',{'action':'mail.delete','values':{'id':'2'},'confirmed':True});assert r['mutation']['ok'] and not r['state']['confirmation'],r
        r=await post(a,'action',{'action':'chat.open','values':{}});assert r['state']['chat'],r
        r=await post(a,'chat',{'body':'/LEAVE is text'});assert r['mutation']['ok'] and r['state']['chat'] and '/LEAVE is text' in r['text'],r
        r=await post(a,'action',{'action':'chat.exit','values':{}});assert not r['state']['chat'],r
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
        searchable=[]
        for i in range(10):
            r=await post(a,'compose',{'kind':'article','board':1,'title':f'Web pagination {i}','body':'page-marker'})
            assert r['mutation']['ok'],r
            searchable.append(r['mutation']['id'])
        r=await post(a,'action',{'action':'admin.hide','values':{'id':str(searchable[0])},'confirmed':True})
        assert r['mutation']['ok'],r
        search={'action':'board.search','values':{'scope':'HERE','query':'page-marker'},'search_board':1}
        r=await post(b,'action',search)
        assert not r['text'] and [int(row['id']) for row in r['data']['matches']]==searchable[1:9],r
        assert r['data']['next']==searchable[9],r
        r=await post(b,'action',{**search,'cursor':r['data']['next']})
        assert not r['text'] and [int(row['id']) for row in r['data']['matches']]==searchable[9:],r
        assert r['data']['next']==0,r
        plain='visible line\n'*700
        r=await a.post(base+'upload',data=plain.encode(),headers={**headers,'X-File-Name':enc('view.txt')})
        assert r.status==200,await r.text()
        text_id=str((await r.json())['id'])
        r=await post(a,'action',{'action':'file.ascii','values':{'id':text_id,'value':'ON'}})
        assert r['mutation']['ok'],r
        view_text={'action':'file.text','values':{'id':text_id}}
        r=await post(b,'action',view_text)
        assert r['data']['next'] and not r['text'],r
        first=r['data']['file_text']['body']
        r=await post(b,'action',{**view_text,'cursor':r['data']['next']})
        assert first+r['data']['file_text']['body']==plain and not r['data']['next'],r
        r=await post(a,'action',{'action':'admin.connections'})
        assert not r['text'] and r['data']['report']['title']=='CONNECTIONS',r
        assert {'alice','bob','carol'}<={row[1] for row in r['data']['report']['rows']},r
        r=await post(a,'action',{'action':'admin.members'})
        assert not r['text'] and [row[1] for row in r['data']['report']['rows']]==['alice','bob','carol'],r
        r=await post(a,'action',{'action':'admin.applications'})
        assert not r['text'] and not r['data']['report']['rows'],r
        r=await post(a,'action',{'action':'admin.audit_article','values':{'id':str(searchable[0])}})
        assert not r['text'] and any(row[4:6]==['HIDE',str(searchable[0])] for row in r['data']['report']['rows']),r
        r=await post(a,'action',{'action':'settings.prefs'})
        assert not r['text'] and r['data']['report']['title']=='PREFS',r
        r=await post(b,'action',{'action':'board.check','values':{'scope':'ALL'}})
        assert r['data']['check']=={'active':True,'finished':False} and r['data']['next'],r
        r=await post(b,'action',{'action':'check.done'})
        assert r['text'].startswith('ERR ') and r['data']['check']['active'],r
        r=await post(b,'action',{'action':'check.next'})
        assert r['data']['check']=={'active':True,'finished':True},r
        r=await post(b,'action',{'action':'check.done'})
        assert r['text'].startswith('CHECKPOINT SAVED'),r
        print('PASS Web HTTP: CSRF/Origin, session isolation, 256KiB drafts, Gather, literal posts, mail/privacy, public/private files with SHA256, one-use downloads, resume/retry, terminal login and real PTY door return')
    finally:
        await asyncio.gather(*(c.close() for c in clients));await runner.cleanup()
        controller.close();await controller.wait_closed()

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
