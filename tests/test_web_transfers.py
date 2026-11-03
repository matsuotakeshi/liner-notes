"""Large HTTP transfers and recovery against a disposable real BBS."""
import asyncio,base64,hashlib,json,os,socket,subprocess,sys,tempfile,time
from pathlib import Path
import aiohttp
from aiohttp import web
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from webui.app import create_app
from webui.bridge import BridgeError
from test_board import Server

async def exercise(server,root,fixture):
    sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
    origin=f'http://127.0.0.1:{port}';base=origin+'/notes/api/'
    config={'web':{'origin':origin,'listen':'127.0.0.1','secure_cookies':False,'spool':str(root/'spool')},'bbs':{'host':'127.0.0.1','port':server.port,'telnet':True}}
    app=create_app(config);runner=web.AppRunner(app);await runner.setup();await web.TCPSite(runner,'127.0.0.1',port).start()
    clients=[]
    async def client(name):
        c=aiohttp.ClientSession(headers={'Origin':origin},cookie_jar=aiohttp.CookieJar(unsafe=True),timeout=aiohttp.ClientTimeout(total=900));clients.append(c)
        async with c.post(base+'session',json={}) as r:v=await r.json();c.headers['X-CSRF-Token']=v['csrf']
        async with c.post(base+'auth',json={'login':name,'password':'web-transfer-test','register':True}) as r:
            v=await r.json();assert v['state']['user'],v;c.headers['X-CSRF-Token']=v['csrf']
        return c
    async def post(c,path,payload):
        async with c.post(base+path,json=payload) as r:assert r.status==200,await r.text();return await r.json()
    encode=lambda x:base64.b64encode(x.encode()).decode()
    headers={'X-File-Name':encode('resume.bin'),'X-File-Description':encode('HTTP transfer regression')}
    try:
        a=await client('alice');b=await client('bob');outsider=await client('carol')
        session=next(s for s in app['sessions'].values() if s.bridge.state['username']=='alice');assert session.bridge.state['transfer_block']==65536
        # Simulate an upstream error after one durable block. The user receives a real ID.
        payload=bytes(range(256))*1000;original=session.bridge.exchange;calls=0
        async def interrupted(command=None,**kwargs):
            nonlocal calls
            if command and command.startswith('FILEPUTBLOCK '):
                calls+=1
                if calls==2:raise BridgeError('Simulated interrupted block')
            return await original(command,**kwargs)
        session.bridge.exchange=interrupted
        async with a.post(base+'upload',data=payload,headers=headers) as r:
            v=await r.json();assert r.status==409 and v['resume_id'] and v['sent']==65536,v;ident=v['resume_id']
        session.bridge.exchange=original
        async with a.get(base+'resumes') as r:rows=(await r.json())['uploads'];assert len(rows)==1 and rows[0]['id']==ident,rows
        async with b.get(base+'resumes') as r:assert not (await r.json())['uploads']
        # Recreate the metadata reader to prove recovery does not depend on an in-memory session.
        from webui.transfers import ResumeRecords
        assert ResumeRecords(root/'spool',config['bbs']).list(session.bridge.state)[0]['id']==ident
        async with a.post(base+'upload',data=b'different',headers={**headers,'X-Upload-Resume':str(ident)}) as r:assert r.status==409
        async with a.post(base+'upload',data=payload,headers={**headers,'X-Upload-Resume':str(ident)}) as r:assert r.status==200,await r.text()
        async with a.get(base+'resumes') as r:assert not (await r.json())['uploads']
        # Attach while a resumed/active mail editor is open. The draft must survive transfer.
        result=await post(a,'input',{'line':'MAIL SEND bob attached'});assert result['state']['editing'],result
        async with a.post(base+'upload?kind=mail',data=payload,headers=headers) as r:
            v=await r.json();assert r.status==200,v;attachment=v['id']
        assert session.bridge.state['editing']
        result=await post(a,'compose',{'kind':'editing','title':'attached','body':'draft survives transfer','attachments':[attachment]});assert 'MAIL SENT' in result['text'],result
        async with b.post(base+'download',json={'id':attachment,'kind':'mail'}) as r:assert r.status==200;assert await r.read()==payload
        async with outsider.post(base+'download',json={'id':attachment,'kind':'mail'}) as r:assert r.status==409
        assert not (await post(a,'input',{'line':'PING'}))['state']['editing']
        print('PASS block transfer, durable resume ID, owner isolation, mismatched source, mail editor preservation and private attachments',flush=True)
        if os.environ.get('LN_WEB_BENCHMARK'):
            bsession=next(s for s in app['sessions'].values() if s.bridge.state['username']=='bob');bread=bsession.bridge.exchange
            async def legacy_a(command=None,**kw):
                r=await original(command,**kw);session.bridge.state.pop('transfer_block',None);return r
            async def legacy_b(command=None,**kw):
                r=await bread(command,**kw);bsession.bridge.state.pop('transfer_block',None);return r
            timings={}
            for mode in ('legacy','block'):
                session.bridge.exchange=legacy_a if mode=='legacy' else original
                bsession.bridge.exchange=legacy_b if mode=='legacy' else bread
                at=time.monotonic()
                async with a.post(base+'upload',data=payload,headers=headers) as r:assert r.status==200,await r.text();fid=(await r.json())['id']
                uploaded=time.monotonic()-at;at=time.monotonic()
                async with b.post(base+'download',json={'id':fid}) as r:assert r.status==200,await r.text();assert await r.read()==payload
                timings[mode]={'bytes':len(payload),'upload_seconds':round(uploaded,3),'download_seconds':round(time.monotonic()-at,3)}
            Path('web-transfer-benchmark.json').write_text(json.dumps(timings,indent=2));print('PASS identical-payload benchmark '+json.dumps(timings),flush=True)
        if fixture:
            expected=hashlib.file_digest(fixture.open('rb'),'sha256').hexdigest();size=fixture.stat().st_size
            start=time.monotonic()
            with fixture.open('rb') as data:
                async with a.post(base+'upload',data=data,headers={**headers,'X-Upload-Async':'1','X-File-Name':encode('OBS-Studio-32.2.2-Windows-x64-Installer.exe')}) as r:
                    accepted=await r.json();assert r.status==202,accepted;token=accepted['transfer']
            accepted_seconds=time.monotonic()-start
            async with outsider.get(base+'transfer/'+token) as r:assert r.status==404
            while True:
                async with a.get(base+'transfer/'+token) as r:assert r.status==200;job=await r.json()
                if job['done']:break
                assert time.monotonic()-start<900
                await asyncio.sleep(.5)
            v=job['result'];assert job['http_status']==200,v;assert v['sha256']==expected and v['size']==size,v;ident=v['id']
            uploaded=time.monotonic()-start;print(f'PASS actual OBS upload: {size} bytes, {uploaded:.2f}s',flush=True)
            start=time.monotonic();received=0;digest=hashlib.sha256()
            async with b.post(base+'download',json={'id':ident}) as r:
                assert r.status==200,await r.text();assert r.content_length==size
                async for chunk in r.content.iter_chunked(65536):received+=len(chunk);digest.update(chunk)
            downloaded=time.monotonic()-start;assert received==size and digest.hexdigest()==expected
            result={'bytes':size,'sha256':expected,'accepted_seconds':round(accepted_seconds,2),'upload_seconds':round(uploaded,2),'download_seconds':round(downloaded,2),'verified':True,'asynchronous':True}
            Path('web-large-result.json').write_text(json.dumps(result,indent=2));print('PASS actual OBS roundtrip '+json.dumps(result),flush=True)
    finally:
        for c in clients:await c.close()
        await runner.cleanup()

def main():
    binary=str(Path(sys.argv[1]).resolve());ctl=str(Path(sys.argv[2]).resolve());fixture=Path(sys.argv[3]).resolve() if len(sys.argv)>3 else None
    with tempfile.TemporaryDirectory(prefix='ln-web-transfer-') as directory:
        root=Path(directory);subprocess.run([ctl,'init',str(root/'vault')],check=True,capture_output=True)
        server=Server(binary,root/'notes',members=root/'members',vault=root/'vault',telnet=True,language='en')
        try:asyncio.run(exercise(server,root,fixture))
        finally:server.close()

if __name__=='__main__':main()
