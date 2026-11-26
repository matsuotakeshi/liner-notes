"""Web line conference through a real connector and temporary BBS."""
import asyncio
from contextlib import closing
from pathlib import Path
import socket
import sys
import tempfile

import aiohttp
from aiohttp import web

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from test_board import Server
from webui.app import create_app
from webui.bridge import Bridge


async def exercise(bbs, directory):
    listen = socket.socket()
    listen.bind(("127.0.0.1", 0))
    origin = "http://127.0.0.1:" + str(listen.getsockname()[1])
    app = create_app({
        "web": {"origin": origin, "secure_cookies": False, "spool": str(directory / "spool")},
        "bbs": {"host": "127.0.0.1", "port": bbs.port, "telnet": True},
    })
    async with aiohttp.ClientSession(cookie_jar=aiohttp.CookieJar(unsafe=True)) as first, \
               aiohttp.ClientSession(cookie_jar=aiohttp.CookieJar(unsafe=True)) as second:
        runner = web.AppRunner(app)
        await runner.setup()
        await web.SockSite(runner, listen).start()
        base = origin + "/notes/api/"

        async def call(client, path, payload, status=200):
            async with client.post(base + path, json=payload) as response:
                assert response.status == status, (response.status, await response.text())
                return await response.json() if status == 200 else None

        async def login(client, name):
            info = await call(client, "auth", {
                "login": name, "password": "test-conference-pass", "register": name != "alice",
            })
            assert info["state"]["user"] and info["state"]["core_conference"] == 1, info
            client.headers["X-CSRF-Token"] = info["csrf"]

        async def conference(client, action, **kwargs):
            return await call(client, "conference", dict(action=action, **kwargs))

        try:
            for client in (first, second):
                client.headers["Origin"] = origin
                info = await call(client, "session", {})
                client.headers["X-CSRF-Token"] = info["csrf"]
            await call(second, "conference", {"action": "rooms"}, 403)
            await login(first, "alice")
            await login(second, "bob")

            rooms = await conference(second, "rooms")
            assert rooms["data"]["conference"]["rooms"][0]["id"] == 1, rooms
            denied = await conference(second, "create", room=2, body="Denied")
            assert not denied["mutation"]["ok"], denied
            created = await conference(first, "create", room=2, body="会議室")
            assert created["mutation"]["ok"] and created["mutation"]["id"] == 2, created
            await call(second, "conference", {"action": "history", "room": True}, 400)
            await call(second, "conference", {"action": "post", "room": 2, "body": "bad\nline"}, 400)

            message = '日本語 | "quoted" <script>'
            sent = await conference(first, "post", room=2, body=message)
            ident = sent["mutation"]["id"]
            assert sent["mutation"]["ok"] and ident > 0, sent
            received = await conference(second, "unread", room=2)
            page = received["data"]["conference"]
            assert page["through"] == ident and page["lines"][0]["text"] == message, page
            before = await conference(second, "position", room=2)
            assert before["data"]["conference"]["through"] == 0, before
            seen = await conference(second, "seen", room=2, cursor=ident)
            assert seen["mutation"]["ok"], seen
            assert not (await conference(second, "unread", room=2))["data"]["conference"]["lines"]
            assert (await conference(first, "unread", room=2))["data"]["conference"]["lines"]

            bridge = await Bridge("127.0.0.1", bbs.port).open()
            try:
                await bridge.command("LOGIN bob")
                await bridge.command("test-conference-pass")
                history = await bridge.command("LINECONF HISTORY 2")
                assert message in history["text"], history
                sent = await bridge.command("LINECONF POST 2 From the terminal")
                assert "CONFERENCE SAVED" in sent["text"], sent
            finally:
                await bridge.close()
            history = await conference(first, "history", room=2)
            assert [row["text"] for row in history["data"]["conference"]["lines"]] == [message, "From the terminal"]
        finally:
            await runner.cleanup()


async def seed(bbs):
    bridge = await Bridge("127.0.0.1", bbs.port).open()
    try:
        await bridge.command("REGISTER alice")
        result = await bridge.command("test-conference-pass")
        assert result["state"]["user"] == 1, result
    finally:
        await bridge.close()


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="ln-web-conference-") as name:
        directory = Path(name)
        board, members, admins = (directory / path for path in ("notes", "members", "admins"))
        options = ["--database-structured", str(directory / "liner.sqlite")]
        with closing(Server(binary, board, members=members, telnet=True, options=options)) as bootstrap:
            asyncio.run(seed(bootstrap))
        admins.write_text("1\n", encoding="ascii")
        with closing(Server(binary, board, members=members, telnet=True, admins=admins, options=options)) as bbs:
            asyncio.run(exercise(bbs, directory))
    print("PASS Web conference: authentication, admin ACL, typed views and writes, independent unread positions, RTBBS round trip")
