"""Real connector Core regression in journal and authoritative SQLite modes.

Peer signature/ring tests remain in test_link_bbs/test_shared_visibility. This
test exercises the local authenticated connector, not an unsigned peer ingress.
"""
import asyncio
from contextlib import closing
from pathlib import Path
import sqlite3
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / "scripts")]
from link_bbs import Core
from webui.bridge import Bridge
from test_board import Server

PASSWORD = "link-core-test-password"


async def command(client, text, expected):
    response = (await client.command(text))["text"]
    assert expected in response, (text, response)


async def rejected(core, event, expected, parent=0):
    try:
        await core.apply(event, parent)
    except ValueError as error:
        assert expected in str(error), error
    else:
        raise AssertionError(("unexpected event acceptance", event["kind"]))


async def scenario(binary, root, sqlite):
    options = ["--database", str(root / "store.sqlite")] if sqlite else []

    def start(link=False):
        return Server(binary, root / "notes", members=root / "members", telnet=True,
                      admins=root / "admins" if link else None,
                      options=options + (["--link-accounts", str(root / "accounts")] if link else []))

    with closing(start()) as seed:
        for name in ("link", "admin"):
            client = await Bridge("127.0.0.1", seed.port).open()
            try:
                await command(client, "REGISTER " + name, "PASSWORD")
                await command(client, PASSWORD, "LOGGED IN " + name)
            finally:
                await client.close()
    (root / "accounts").write_text("1 1\n", encoding="utf-8")
    (root / "admins").write_text("2\n", encoding="utf-8")
    (root / "secret").write_text(PASSWORD + "\n", encoding="utf-8")
    (root / "secret").chmod(0o600)
    original = dict(origin="a" * 64, article="1" * 64, revision=1, created=123,
                    kind="post", author="alice", title="root secret", body="root body\r\n")
    with closing(start(True)) as server:
        core = await Core(dict(port=server.port, login="link", password_file=str(root / "secret"))).open()
        admin = await Bridge("127.0.0.1", server.port).open()
        try:
            await command(admin, "LOGIN admin", "PASSWORD")
            await command(admin, PASSWORD, "LOGGED IN admin")
            await command(admin, "LINKSNAP 1", "ERR link denied")
            await core.select(1)
            assert await core.lookup(original["origin"], original["article"]) == (0, 0)
            assert await core.apply(original) == 1
            child = dict(original, origin="c" * 64, article="2" * 64, title="child secret", body="child body\r\n")
            grandchild = dict(child, article="3" * 64, title="grandchild secret")
            assert await core.apply(child, 1) == 2
            assert await core.apply(grandchild, 2) == 3
            await command(admin, "WITHDRAW 1 CONFIRM", "ERR imported article; use HIDE")
            await command(admin, "HIDE 1", "HIDDEN SAVED 1")
            for number in (1, 2, 3):
                item = await core.snapshot(number, True)
                assert item["state"] == "HIDDEN" and not item["title"] and not item["body"], item
            assert await core.lookup(child["origin"], child["article"]) == (2, 1)
            await command(admin, "RESTORE 1", "RESTORED 1")
            assert (await core.snapshot(3, True))["body"] == child["body"]
            await rejected(core, dict(original, kind="title", revision=3), "missing predecessor")
            await rejected(core, dict(original, kind="checkpoint", revision=5, body="changed"), "immutable article")
            assert await core.apply(dict(original, kind="withdraw", revision=2, title="", body="")) == 1
            assert await core.apply(original) == 1  # old post cannot resurrect
            await command(admin, "RESTORE 1", "ERR article withdrawn")
            await rejected(core, dict(original, kind="checkpoint", revision=9), "article unavailable")
            for number in (1, 2, 3):
                item = await core.snapshot(number, True)
                assert item["state"] == ("WITHDRAWN" if number == 1 else "HIDDEN")
                assert not item["title"] and not item["body"]
            # Cross the fixed metadata page boundary; IDs remain contiguous.
            for number in range(4, 35):
                assert await core.apply(dict(original, article=f"{number:064x}", title=f"item {number}")) == number
            first, last = await core.page(1), await core.page(33)
            assert len(first) == 32 and [x["id"] for x in last] == [33, 34]
            assert all(not row["title"] and not row["body"] for row in first[:3])
            assert not await core.page(35)
        finally:
            await admin.close()
            await core.close()
    # A durable intent whose effect is already present is replayed at startup.
    database = root / "store.sqlite" if sqlite else Path(str(root / "notes") + ".features.sqlite")
    with closing(sqlite3.connect(database)) as db:
        db.execute("UPDATE link_articles SET done=0")
        db.commit()
    with closing(start(True)) as server:
        core = await Core(dict(port=server.port, login="link", password_file=str(root / "secret"))).open()
        try:
            await core.select(1)
            assert await core.lookup(original["origin"], original["article"]) == (1, 2)
            item = await core.snapshot(3, True)
            assert item["parent"] == 2 and item["state"] == "HIDDEN" and not item["title"] and not item["body"]
            assert [x["id"] for x in await core.page(33)] == [33, 34]
            assert await core.apply(original) == 1
            assert not await core.snapshot(35)
        finally:
            await core.close()
    print("PASS: link Core authority, visibility, checkpoints, paging, replay and restart", "SQLite" if sqlite else "journal")


if __name__ == "__main__":
    binary = str(Path(sys.argv[1]).resolve())
    for sqlite in (False, True):
        with tempfile.TemporaryDirectory(prefix="link-core-") as directory:
            asyncio.run(scenario(binary, Path(directory), sqlite))
