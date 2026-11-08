"""Origin authority, local moderation, delayed mixed-origin trees and HTTP.

All runtimes/listeners/databases are temporary and loopback-only. No home or
production server is contacted. Signatures use ephemeral Ed25519 keys.
"""
import asyncio
import hashlib
import json
import sys
import tempfile
from pathlib import Path

import aiohttp
from aiohttp import web

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'scripts')]
from link_store import Store, Ed25519PrivateKey, identity, public_bytes
from link_bbs import Core, export_board, apply_pending
from webui.bridge import Bridge, response_data
from webui.app import create_app
from test_board import Server


async def exercise(binary, root):
    names = ('link', 'admin', 'sigop', 'alice', 'other')
    password = 'visibility-test-password'
    keys = [Ed25519PrivateKey.generate() for _ in range(3)]
    ids = [identity(k.public_key()) for k in keys]
    board = hashlib.sha256(b'visibility-board').hexdigest()
    origins = {n: dict(public=public_bytes(k.public_key()), boards={board}) for n, k in zip(ids, keys)}
    servers, cores, clients, stores, directories = [], [], [], [], []
    ring = [(0, 1), (1, 2), (2, 0)]
    star = [(0, 1), (1, 0), (0, 2), (2, 0)]

    async def command(client, text, expected=None):
        result = (await client.command(text))['text']
        if expected is not None:
            assert expected in result, (text, result)
        return result

    async def connect(server, name):
        b = await Bridge('127.0.0.1', server.port).open()
        await command(b, 'LOGIN ' + name)
        await command(b, password)
        await command(b, 'BOARD 2', 'BOARD 2')
        return b

    async def post(node, title, parent=0):
        b = clients[node]['alice']
        await command(b, f'REPLY {parent} {title}' if parent else 'POST ' + title, 'EDIT ')
        async with b.lock:
            await b.literal(title + '-body')
        text = await command(b, '.', 'SAVED ')
        import re
        return int(re.search(r'SAVED (\d+)', text)[1])

    async def deliver(edges, reverse=False):
        for source, target in edges:
            a, b = stores[source], stores[target]
            events, cursor = a.batch(b.node, b.cursor(a.node))
            for event in reversed(events) if reverse else events:
                b.accept(event, a.node)
                assert b.accept(event, a.node) is False  # duplicate delivery
            b.acknowledge(a.node, cursor)
        for store, core in zip(stores, cores):
            await apply_pending(store, core, {board: 2})

    async def sync(edges, reverse=False):
        for _ in range(5):
            for store, core in zip(stores, cores):
                await export_board(store, core, board, 2)
            await deliver(edges, reverse)

    async def concealed(node, numbers):
        b = clients[node]['other']
        for number in numbers:
            text = await command(b, f'READ {number}')
            assert 'ARTICLE ' not in text and 'needle' not in text, text
            assert not response_data(text).get('article'), text
            await command(b, f'REPLY {number} forbidden', 'ERR article hidden')
            await command(b, f'GET {number}', 'ERR no article')
        for query in ('LIST', 'NOTES', 'TREE 1', 'SEARCH needle',
                      'FIND HERE * needle', 'FIND SUBTREE * needle', 'FIND ALL * needle',
                      'UNREAD', 'NEWS ALL 2000-01-01'):
            text = await command(b, query)
            assert 'needle' not in text, (query, text)
        await cores[node].select(2)
        for number in numbers:
            item = await cores[node].snapshot(number, True)
            assert item['state'] != 'LIVE' and not item['title'] and not item['body'], item

    try:
        for i in range(3):
            d = root / str(i); d.mkdir(); directories.append(d)
            seed = Server(binary, d/'notes', members=d/'members', telnet=True)
            for name in names:
                b = await Bridge('127.0.0.1', seed.port).open()
                await command(b, 'REGISTER ' + name)
                await command(b, password)
                await b.close()
            seed.close()
            (d/'admins').write_text('2\n')
            (d/'catalog').write_text('1|0|all|member|Forum\n2|1|all|member|Shared\n')
            (d/'accounts').write_text('2 1\n')
            (d/'secret').write_text(password+'\n'); (d/'secret').chmod(0o600)
            server = Server(binary, d/'notes', members=d/'members', catalog=d/'catalog',
                            admins=d/'admins', telnet=True, options=['--link-accounts', str(d/'accounts')])
            servers.append(server)
            admin = await connect(server, 'admin')
            await command(admin, 'SIGOP 1 3', 'SIGOP SET')
            clients.append({'admin': admin})
            for name in ('sigop', 'alice', 'other'):
                clients[i][name] = await connect(server, name)
            cores.append(await Core(dict(port=server.port, login='link', password_file=str(d/'secret'))).open())
            stores.append(Store(d/'link.sqlite', keys[i], origins, {n: v for n, v in origins.items() if n != ids[i]}))

        assert await post(0, 'needle-root') == 1
        await sync(ring)
        assert await post(1, 'needle-child', 1) == 2
        await sync(star)
        assert await post(2, 'needle-grandchild', 2) == 3
        await sync(ring)
        for i in range(3):
            await command(clients[i]['other'], 'WITHDRAW 1 CONFIRM', 'ERR article owner')
            await command(clients[i]['other'], 'HIDE 1', 'ERR admin required')
        # Origin-based authority survives a local author rename.
        await command(clients[1]['admin'], 'AUTHOR 1 2 CONFIRM', 'AUTHOR')
        for role in ('admin', 'sigop'):
            await command(clients[1][role], 'WITHDRAW 1 CONFIRM', 'ERR imported article; use HIDE')
        await command(clients[1]['sigop'], 'HIDE 1', 'HIDDEN SAVED 1')
        before = stores[1].db.execute('SELECT count(*) FROM events WHERE origin=?', (ids[1],)).fetchone()
        await sync(star)
        assert stores[1].db.execute('SELECT count(*) FROM events WHERE origin=?', (ids[1],)).fetchone() == before
        await concealed(1, (1, 2, 3))
        for i in (0, 2):
            assert response_data(await command(clients[i]['alice'], 'READ 3'))['article']['parent'] == 2
        # A reply arriving below a locally hidden parent must still be stored.
        assert await post(0, 'needle-late', 3) == 4
        await sync(ring)
        await concealed(1, (4,))
        await command(clients[1]['admin'], 'RESTORE 1', 'RESTORED 1')
        assert response_data(await command(clients[1]['alice'], 'READ 4'))['article']['parent'] == 3

        # HTTP uses the same core decision, including the administrator view.
        app = create_app({'web': {'origin': 'http://visibility.invalid', 'secure_cookies': False,
                                 'spool': str(root/'spool'), 'messages': str(ROOT/'webui/messages')},
                          'bbs': {'host': '127.0.0.1', 'port': servers[1].port, 'telnet': True}})
        runner = web.AppRunner(app); await runner.setup()
        site = web.TCPSite(runner, '127.0.0.1', 0); await site.start()
        port = site._server.sockets[0].getsockname()[1]
        try:
            async with aiohttp.ClientSession(cookie_jar=aiohttp.CookieJar(unsafe=True), headers={'Origin': 'http://visibility.invalid'}) as http:
                base = f'http://127.0.0.1:{port}/notes/api/'
                async def request(route, payload):
                    async with http.post(base+route, json=payload) as r:
                        assert r.status == 200, await r.text()
                        data = await r.json()
                        http.headers['X-CSRF-Token'] = data['csrf']
                        return data
                await request('session', {})
                await request('auth', dict(login='other', password=password))
                await request('input', dict(line='BOARD 2'))
                visible = await request('query', dict(kind='read', id=3))
                assert visible['data']['article']['title'] == 'needle-grandchild'
                # Withdrawal by inherited origin SIGOP propagates to every node.
                await command(clients[0]['sigop'], 'WITHDRAW 1 CONFIRM', 'WITHDRAW SAVED 1')
                await sync(star, reverse=True)
                for payload in (dict(kind='read', id=3), dict(kind='notes')):
                    result = await request('query', payload)
                    assert 'needle' not in json.dumps(result['data']) and not result['data'].get('article'), result
                result = await request('input', dict(line='FIND ALL * needle'))
                assert 'needle-' not in json.dumps(result['data']), result
        finally:
            await runner.cleanup()
        for i in range(3):
            await concealed(i, (1, 2, 3, 4))
            await command(clients[i]['admin'], 'RESTORE 1', 'ERR article withdrawn')
            # A child-local restoration cannot override its withdrawn ancestor.
            await command(clients[i]['admin'], 'RESTORE 3', 'RESTORED 3')
            await concealed(i, (3,))
            await command(clients[i]['admin'], 'INSPECT 3', 'needle-grandchild-body')
        # Local author and origin administrator retain their own authority.
        for role in ('alice', 'admin'):
            number = await post(0, 'withdraw-test-'+role)
            await sync(ring)
            await command(clients[0][role], f'WITHDRAW {number} CONFIRM', 'WITHDRAW SAVED')
            await sync(ring)
            for i in range(3):
                await command(clients[i]['other'], f'READ {number}', 'WITHDRAWN')

        # Long-disconnected/new participant: reversed signed checkpoints, old
        # replay, missing parents, durable deferred events, and local restart.
        old_events = [json.loads(row[0]) for row in stores[0].db.execute('SELECT envelope FROM events')]
        for store in stores:
            store.make_checkpoints()
        await sync(star)
        for store in stores:
            store.compact()
        d = root/'new'; d.mkdir()
        seed = Server(binary, d/'notes', members=d/'members', telnet=True)
        b = await Bridge('127.0.0.1', seed.port).open()
        await command(b, 'REGISTER link'); await command(b, password); await b.close(); seed.close()
        (d/'accounts').write_text('2 1\n'); (d/'secret').write_text(password+'\n')
        key = Ed25519PrivateKey.generate(); node = identity(key.public_key())
        new_origins = dict(origins, **{node: dict(public=public_bytes(key.public_key()), boards={board})})
        fresh = Store(d/'link.sqlite', key, new_origins, origins)
        new_server = Server(binary, d/'notes', members=d/'members', catalog=directories[0]/'catalog',
                            telnet=True, options=['--link-accounts', str(d/'accounts')])
        probe = await Core(dict(port=new_server.port, login='link', password_file=str(d/'secret'))).open()
        try:
            envelopes = [json.loads(row[0]) for row in stores[0].db.execute('SELECT envelope FROM events ORDER BY seq')]
            for envelope in reversed(envelopes):
                fresh.accept(envelope, ids[0])
            await apply_pending(fresh, probe, {board: 2})
            await probe.close(); new_server.close(); fresh.close()
            fresh = Store(d/'link.sqlite', key, new_origins, origins)
            new_server = Server(binary, d/'notes', members=d/'members', catalog=directories[0]/'catalog',
                                telnet=True, options=['--link-accounts', str(d/'accounts')])
            probe = await Core(dict(port=new_server.port, login='link', password_file=str(d/'secret'))).open()
            for _ in range(8):
                await apply_pending(fresh, probe, {board: 2})
            assert not fresh.pending(), fresh.pending()
            for envelope in old_events:
                assert fresh.accept(envelope, ids[0]) is False
            await probe.select(2)
            items = await probe.page(1)
            assert len(items) == 6, items
            assert all(item['state'] != 'LIVE' and not item['title'] for item in items), items
            # Original identities and all four parent relationships survive,
            # although arrival order may assign different *local* IDs.
            by_key = {(item['origin'], item['article']): item for item in items}
            source_items = await cores[0].page(1)
            from link_bbs import article_key
            source_identity = {item['id']: (ids[0], article_key(ids[0], board, item['id'])) if item['origin']=='-' else (item['origin'], item['article']) for item in source_items}
            for item in source_items:
                copied = by_key[source_identity[item['id']]]
                assert copied['parent'] == (by_key[source_identity[item['parent']]]['id'] if item['parent'] else 0)
        finally:
            await probe.close(); new_server.close(); fresh.close()
        print('PASS mixed-origin 4-level star/ring; origin author/admin/inherited SIGOP; receiver local hide; HTTP redaction; delayed reply; reversed checkpoints; replay and restart')
    finally:
        for group in clients:
            for b in group.values(): await b.close()
        for core in cores: await core.close()
        for server in servers:
            if server.proc.poll() is None: server.close()
        for store in stores: store.close()


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='shared-visibility-') as tmp:
        asyncio.run(exercise(str(Path(sys.argv[1]).resolve()), Path(tmp)))
