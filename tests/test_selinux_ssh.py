"""Hosted real SSH fixture regression. Deliberately no SELinux/VM claim."""
import asyncio
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'ci'), str(ROOT)]
from selinux_ssh import key_fixture, probe
from selinux_native import bbs_probe
from test_selinux_bootstrap import UnconfinedLinkFixture
from test_board import Server


def run(binary):
    with tempfile.TemporaryDirectory(prefix='ln-hosted-ssh-policy-') as temporary:
        fixture = UnconfinedLinkFixture(Path(temporary) / 'fixture')
        keys = key_fixture(fixture)
        core = fixture.directory('core', 'unused-in-hosted')
        server = Server(binary, core / 'notes', members=core / 'members',
                        menu=True, telnet=True, language='en', options=[
                            '--ssh-port', str(keys['port']), '--ssh-host-key', str(keys['host']),
                            '--ssh-member-keys', str(keys['keys'])])
        try:
            asyncio.run(bbs_probe(server.port, create=True))
            probe(keys['port'], keys['host'], keys['client'])
            asyncio.run(bbs_probe(server.port, create=False))
        finally:
            server.close()
        print('PASS Hosted SSH acceptance client and Telnet coexistence; MAC is NOT tested here')


if __name__ == '__main__':
    run(str(Path(sys.argv[1]).resolve()))
