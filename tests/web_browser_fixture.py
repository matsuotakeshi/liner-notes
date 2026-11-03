"""Disposable interactive browser fixture. Terminate to remove all generated state."""
import asyncio,signal,subprocess,sys,tempfile
from pathlib import Path
from aiohttp import web
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from webui.app import create_app
from webui.bridge import Bridge
from test_board import Server

async def serve(root,binary,ctl,port):
    subprocess.run([ctl,'init',str(root/'vault')],capture_output=True,check=True)
    bootstrap=Server(binary,root/'notes',members=root/'members',telnet=True)
    try:
        c=await Bridge('127.0.0.1',bootstrap.port).open()
        await c.command('REGISTER alice');await c.command('browser-test-password');await c.close()
    finally:bootstrap.close()
    admins=root/'admins';admins.write_text('1\n')
    catalog=root/'boards';catalog.write_text('1|0|all|member|General\n2|1|all|member|Child board\n')
    bbs=Server(binary,root/'notes',members=root/'members',catalog=catalog,admins=admins,telnet=True,menu=True,language='en',vault=root/'vault')
    config={'web':{'listen':'127.0.0.1','origin':f'http://127.0.0.1:{port}','secure_cookies':False,'spool':str(root/'spool')},'bbs':{'host':'127.0.0.1','port':bbs.port,'telnet':True}}
    runner=web.AppRunner(create_app(config));await runner.setup();await web.TCPSite(runner,'127.0.0.1',port).start()
    done=asyncio.Event();loop=asyncio.get_running_loop()
    for sig in (signal.SIGTERM,signal.SIGINT):loop.add_signal_handler(sig,done.set)
    print(f'BROWSER FIXTURE {port}',flush=True)
    try:await done.wait()
    finally:await runner.cleanup();bbs.close()

if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='ln-web-browser-') as tmp:asyncio.run(serve(Path(tmp),str(Path(sys.argv[1]).resolve()),str(Path(sys.argv[2]).resolve()),int(sys.argv[3])))
