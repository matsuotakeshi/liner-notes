"""Hosted real-runtime regression for native acceptance bootstrap; no root/MAC.

This validates CLI, connector, membership and persistence contracts only.
It does not claim SELinux Enforcing acceptance.
"""
import asyncio
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'ci'), str(ROOT / 'scripts')]
from test_board import Server
from selinux_native import bbs_probe, core_arguments
from link_bbs import Core


async def verify_link(port, password):
    client = Core({'host': '127.0.0.1', 'port': port, 'login': 'link',
                   'password_file': str(password)})
    await client.open()
    try:
        assert client.bridge.state['user'] == 3
        await client.select(1)
        article = await client.snapshot(1, body=True)
        assert article['id'] == 1
        assert 'Persisted under Enforcing' in article['body']
    finally:
        await client.close()


def run(binary, directory):
    accounts = directory / 'link-accounts'
    accounts.write_text('1 3\n', encoding='utf-8')
    password = directory / 'password'
    password.write_text('selinux-only-fixture-password\n', encoding='utf-8')
    password.chmod(0o600)

    # Demonstrate why the original native fixture could never start. This is a
    # separate fresh instance; it must not alter the successful instance below.
    missing = directory / 'missing'
    missing.mkdir()
    failure = subprocess.run([binary, '--database-structured', str(missing / 'notes.sqlite'),
        '--link-accounts', str(accounts), '0', str(missing / 'notes'), str(missing / 'members')],
        capture_output=True, timeout=15)
    assert failure.returncode != 0, failure.stdout
    assert b'link: configuration or recovery failed' in failure.stderr, failure.stderr

    live = directory / 'live'
    live.mkdir()
    arguments = ['--database-structured', str(live / 'notes.sqlite'),
                 '--link-accounts', str(accounts)]
    original = list(arguments)
    for iteration in range(3):
        # Use exactly the native fixture's selector and registration probe, so a
        # forgotten bootstrap exception or missing link registration fails here.
        current = core_arguments(arguments, bootstrap=iteration == 0)
        assert arguments == original, 'Argument selection changed the reusable configuration'
        server = Server(binary, live / 'notes', members=live / 'members',
                        telnet=True, menu=True, language='en', options=current)
        try:
            asyncio.run(bbs_probe(server.port, create=iteration == 0))
            if iteration:
                asyncio.run(verify_link(server.port, password))
        finally:
            server.close()
    print('PASS: real runtime bootstrap, members 1/2/3, link allowlist, articles/mail and two restarts')
    print('Scope: no root, home VM, installed service or SELinux acceptance')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='ln-selinux-bootstrap-') as temporary:
        run(str(Path(sys.argv[1]).resolve()), Path(temporary))
