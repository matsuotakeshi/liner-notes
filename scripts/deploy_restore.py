"""Read-only verification of backup copies and configured local health probes."""
import hashlib
import contextlib
from pathlib import Path
import socket
import sqlite3
import time
from deploy_policy import digest_file, require


def verify_restored(restored, roots, checks):
    require(checks, 'Application data verification queries are required')
    for root in roots:
        original = Path(root)
        files = original.rglob('*') if original.is_dir() else [original]
        for path in files:
            if path.is_symlink():
                target = restored / str(path).lstrip('/')
                require(target.is_symlink() and target.readlink() == path.readlink(), 'Restored symlink differs')
            elif path.is_file():
                target = restored / str(path).lstrip('/')
                require(target.is_file() and not target.is_symlink() and digest_file(path) == digest_file(target), 'Restored data differs from stopped source')
    for check in checks:
        source = Path(check['path'])
        require(source.is_absolute() and any(source.is_relative_to(Path(root)) for root in roots), 'Database check is outside backup inventory')
        copy = restored / str(source).lstrip('/')
        def rows(path):
            with contextlib.closing(sqlite3.connect(path.as_uri() + '?mode=ro', uri=True)) as db:
                db.execute('PRAGMA query_only=ON')
                require(check['query'].lstrip().upper().startswith('SELECT '), 'Restore verification requires SELECT')
                return db.execute(check['query']).fetchall()
        require(rows(source) == rows(copy), 'Restored application records differ')


def health_probes(probes):
    """Only local TCP exchanges; no shell, filesystem mutation or external host.

    A root-maintained profile supplies the private acceptance account and exact
    transcript expectations. Probe payloads and responses are never reported.
    Do not put private profiles into Actions artifacts.
    """
    for name, probe in probes.items():
        require(probe['host'] in ('127.0.0.1', '::1'), 'Health probes must stay on this host')
        require(type(probe['port']) is int and 0 < probe['port'] < 65536 and probe['exchanges'], 'Invalid health probe')
        with socket.create_connection((probe['host'], probe['port']), timeout=10) as stream:
            stream.settimeout(10)
            for exchange in probe['exchanges']:
                if exchange.get('send'):
                    stream.sendall(exchange['send'].encode('utf-8'))
                expected = exchange['expect'].encode('utf-8')
                require(expected, 'Health check expectation must not be empty')
                received = bytearray()
                deadline = time.monotonic() + 10
                while expected not in received and len(received) < 1024 * 1024 and time.monotonic() < deadline:
                    block = stream.recv(8192)
                    require(block, 'Health probe ended early')
                    received.extend(block)
                require(expected in received, 'Health probe did not match its expected response')
