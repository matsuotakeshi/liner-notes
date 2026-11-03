"""A browser terminal connects only to the configured BBS, never an OS shell."""
import asyncio
import json
import time
from aiohttp import web, WSMsgType
from .bridge import TelnetDecoder


async def relay(request, session, upstream):
    protocols={x.strip() for x in request.headers.get('Sec-WebSocket-Protocol','').split(',')}
    if 'csrf.'+session.csrf not in protocols:raise web.HTTPForbidden()
    if session.terminal:raise web.HTTPConflict(text='A terminal is already open')
    session.terminal=True
    writer=None;task=None
    ws=web.WebSocketResponse(protocols=('liner-notes-v1',),heartbeat=30,max_msg_size=16384)
    session.terminal_ws=ws
    try:
        reader,writer=await asyncio.wait_for(asyncio.open_connection(upstream['host'],int(upstream['port'])),15)
        await ws.prepare(request)
        decoder=TelnetDecoder()
        async def output():
            try:
                while data:=await reader.read(32768):
                    if upstream.get('telnet',True):
                        data,reply=decoder.feed(data)
                        if reply:writer.write(reply);await writer.drain()
                    if data:await ws.send_bytes(data)
            finally:await ws.close()
        task=asyncio.create_task(output())
        async for msg in ws:
            if msg.type==WSMsgType.BINARY:
                data=msg.data
                session.bridge.last_browser=time.monotonic()
                writer.write(data.replace(b'\xff',b'\xff\xff') if upstream.get('telnet',True) else data)
                await writer.drain()
            elif msg.type==WSMsgType.TEXT:
                payload=json.loads(msg.data)
                if payload.get('type')!='resize':break
                columns=max(40,min(240,int(payload['columns'])));rows=max(10,min(100,int(payload['rows'])))
                if upstream.get('telnet',True):
                    raw=columns.to_bytes(2,'big')+rows.to_bytes(2,'big')
                    writer.write(b'\xff\xfb\x1f\xff\xfa\x1f'+raw.replace(b'\xff',b'\xff\xff')+b'\xff\xf0');await writer.drain()
    finally:
        if task:task.cancel();await asyncio.gather(task,return_exceptions=True)
        if writer:
            writer.close()
            try:await writer.wait_closed()
            except OSError:pass
        session.terminal=False
        session.terminal_ws=None
        await ws.close()
    return ws
