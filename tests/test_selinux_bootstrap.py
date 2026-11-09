"""Hosted real-runtime regression for native acceptance bootstrap; no root/MAC.

This validates CLI, connector, membership and persistence contracts only.
It does not claim SELinux Enforcing acceptance.
"""
import asyncio
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'ci'), str(ROOT / 'scripts')]
from test_board import Server
from selinux_native import bbs_probe, core_arguments, link_probe, stage_web_source
from link_bbs import Core


class UnconfinedLinkFixture:
    """Hosted-only process harness, explicitly not an SELinux fixture."""

    def __init__(self, path):
        self.path = path
        path.mkdir()
        self.account = SimpleNamespace(pw_uid=os.getuid(), pw_gid=os.getgid())
        self.results, self.processes, self.ports = [], [], set()
        stage_web_source(path / 'source')

    def directory(self, name, _label):
        path = self.path / name
        path.mkdir(mode=0o700)
        return path

    def label(self, *_args):
        pass  # No root, chcon, systemd or home-network access in hosted CI.

    def text(self, name, content, _label, executable=False):
        path = self.path / name
        path.write_text(content, encoding='utf8')
        path.chmod(0o755 if executable else 0o600)
        return path

    def port(self, _label):
        while True:
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', 0))
                port = probe.getsockname()[1]
            if port not in self.ports:
                self.ports.add(port)
                return port

    def start(self, _name, entry, arguments, directory, _domain):
        log = (directory / 'hosted-link.log').open('wb')
        try:
            process = subprocess.Popen([sys.executable, '-B', str(entry), *arguments],
                                       cwd=directory, stdout=log, stderr=subprocess.STDOUT)
        finally:
            log.close()
        self.processes.append(process)
        return process

    def stop(self, process):
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)
            raise


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
            if iteration == 1:
                fixture = UnconfinedLinkFixture(directory / 'hosted-link')
                try:
                    asyncio.run(link_probe(fixture, server.port))
                    assert len(fixture.results) == 1
                finally:
                    for process in fixture.processes:
                        fixture.stop(process)
        finally:
            server.close()
    print('PASS: real runtime bootstrap, members 1/2/3, link allowlist, articles/mail and two restarts')
    print('PASS: native link probe imports signed peer data and pulls actual BBS exports over mutual TLS')
    print('Scope: no root, home VM, installed service or SELinux acceptance')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='ln-selinux-bootstrap-') as temporary:
        run(str(Path(sys.argv[1]).resolve()), Path(temporary))
