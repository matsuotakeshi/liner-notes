"""Templates/storage errors must never be mislabeled as BBS network failures."""
import asyncio,sys,tempfile,logging
from pathlib import Path
from unittest.mock import AsyncMock,patch
from aiohttp.test_utils import TestClient,TestServer
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from webui.app import create_app

async def run():
    with tempfile.TemporaryDirectory(prefix='ln-web-errors-') as tmp:
        p=Path(tmp);templates=p/'templates';templates.mkdir();index=templates/'index.html'
        config={'web':{'origin':'http://example.test','listen':'127.0.0.1','secure_cookies':False,'spool':str(p/'spool'),'templates':str(templates)},'bbs':{'host':'unused.invalid','port':1}}
        async with TestClient(TestServer(create_app(config)),headers={'Host':'example.test','Origin':'http://example.test'}) as client:
            r=await client.get('/notes/');assert r.status==200
            index.write_bytes(b'\xff')
            r=await client.get('/notes/');assert r.status==500;assert 'Web template unavailable' in await r.text()
            index.write_text('<main>Custom template</main>')
            with patch.object(Path,'read_text',side_effect=PermissionError('test template permission')):
                r=await client.get('/notes/');assert r.status==500;assert 'BBS connection unavailable' not in await r.text()
            r=await client.get('/notes/');assert r.status==200 and 'Custom template' in await r.text()
            with patch('webui.bridge.asyncio.open_connection',new=AsyncMock(side_effect=OSError('test network failure'))):
                r=await client.post('/notes/api/session',json={});assert r.status==503;assert (await r.json())['error']=='BBS connection unavailable'
    print('PASS Web errors: missing template fallback, invalid UTF-8/permission errors=500, recovered template=200, BBS network failure=503')

if __name__=='__main__':
    logging.getLogger('webui.app').setLevel(logging.CRITICAL)
    asyncio.run(run())
