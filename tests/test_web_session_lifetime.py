"""HTTP lifetime and storage-failure contracts with one isolated in-memory BBS."""
import asyncio
import base64
from http.cookies import SimpleCookie
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import AsyncMock, patch

from aiohttp import ClientTimeout, WSMsgType, web
from aiohttp.test_utils import TestClient, TestServer
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from webui.app import create_app
from webui.bbs_transport import Bridge


class TestSessionLifetime(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.bridge = Bridge('unused', 1)
        self.bridge.state = dict(user=0)
        cfg = {'web': {'origin':'http://example.test', 'secure_cookies':False,
                       'spool':tmp.name}, 'bbs':{'host':'unused', 'port':1}}
        self.app = create_app(cfg)
        self.request_arrived = asyncio.Event()
        @web.middleware
        async def note_arrival(request, handler):
            if request.method == 'GET' and request.path.endswith('/api/session'):
                self.request_arrived.set()
            return await handler(request)
        self.app.middlewares.insert(0, note_arrival)
        self.client = TestClient(TestServer(self.app), timeout=ClientTimeout(total=8), headers={'Host':'example.test', 'Origin':'http://example.test'})
        await self.client.start_server()
        self.addAsyncCleanup(self.client.close)
        with patch.object(Bridge, 'open', AsyncMock(return_value=self.bridge)):
            r = await self.client.post('/notes/api/session', json={})
        self.assertEqual(r.status, 200)
        self.cookie = next(iter(r.cookies.values())).OutputString().split(';',1)[0]
        self.csrf = (await r.json())['csrf']
        self.headers = {'Cookie':self.cookie, 'X-CSRF-Token':self.csrf}

    async def auth(self):
        return await self.client.post('/notes/api/auth', headers=self.headers,
                                      json={'login':'alice','password':'fixture-password'})

    def install_dialogue(self, game_error=False):
        async def exchange(command):
            if command == 'LOGIN alice':
                self.bridge.state.update(secret=1, password_mode=97)
            elif command == 'fixture-password':
                self.bridge.state.update(user=1, username='alice', secret=0, password_mode=0)
            elif command == 'GAMES' and game_error:
                raise ConnectionError('fixture')
            return {'state':dict(self.bridge.state),'text':''}
        self.bridge.exchange = exchange

    async def test_dialogue_completion_replaces_cookie_and_ends_old_subscription(self):
        self.install_dialogue()
        ws = await self.client.ws_connect('/notes/api/events', headers=self.headers,
                                     protocols=('liner-notes-v1', 'csrf.' + self.csrf))
        self.addAsyncCleanup(ws.close)
        prompt = await self.client.post('/notes/api/input', headers=self.headers, json={'line':'LOGIN alice'})
        challenge = (await prompt.json())['secret_challenge']
        reply = await self.client.post('/notes/api/secret', headers=self.headers,
                                       json={'challenge':challenge,'password':'fixture-password'})
        self.assertEqual(reply.status, 200)
        self.assertNotEqual((await reply.json())['csrf'], self.csrf)
        self.assertTrue(reply.cookies)
        self.assertNotIn(next(iter(SimpleCookie(self.cookie).values())).value, self.app['sessions'])
        message = await asyncio.wait_for(ws.receive(), 3)
        self.assertIn(message.type, (WSMsgType.CLOSE, WSMsgType.CLOSED))
        await ws.close()

    async def test_inflight_guest_request_keeps_its_original_identity(self):
        self.install_dialogue()
        exchange = self.bridge.exchange
        fetching_games, release = asyncio.Event(), asyncio.Event()
        async def gated(command):
            if command == 'GAMES':
                fetching_games.set()
                await release.wait()
            return await exchange(command)
        self.bridge.exchange = gated
        signing_in = asyncio.create_task(self.auth())
        try:
            await asyncio.wait_for(fetching_games.wait(), 3)
            guest_info = asyncio.ensure_future(self.client.get('/notes/api/session', headers=self.headers))
            await asyncio.wait_for(self.request_arrived.wait(), 3)
        finally:
            release.set()
        self.assertEqual((await signing_in).status, 200)
        self.assertEqual((await guest_info).status, 401)

    async def test_error_after_authentication_retires_guest(self):
        self.install_dialogue(game_error=True)
        reply = await self.auth()
        self.assertEqual(reply.status, 503)
        self.assertFalse(self.app['sessions'])
        self.assertTrue(self.bridge.closed)

    async def test_spool_failure_releases_admission(self):
        self.install_dialogue()
        reply = await self.auth()
        headers = {'Cookie':next(iter(reply.cookies.values())).OutputString().split(';',1)[0],
                   'X-CSRF-Token':(await reply.json())['csrf'],
                   'X-File-Name':base64.b64encode(b'note.txt').decode()}
        with patch('webui.app.tempfile.mkstemp', side_effect=OSError('fixture: spool unavailable')):
            failed = await self.client.post('/notes/api/upload', headers=headers, data=b'hello')
        self.assertEqual(failed.status, 500)
        progress = await self.client.get('/notes/api/progress', headers=headers)
        self.assertEqual((await progress.json())['transfers'], [])


if __name__ == '__main__':
    unittest.main()
