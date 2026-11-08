"""Read-only verification of backup copies and configured local health probes."""
import hashlib
import contextlib
import os
from pathlib import Path
import socket
import sqlite3
import shutil
import signal
import subprocess
import tempfile
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


def health_probes(probes, readiness_seconds=30):
    """Only local TCP exchanges; no shell, filesystem mutation or external host.

    A root-maintained profile supplies the private acceptance account and exact
    transcript expectations. Probe payloads and responses are never reported.
    Do not put private profiles into Actions artifacts.
    """
    for name, probe in probes.items():
        require(probe['host'] in ('127.0.0.1', '::1'), 'Health probes must stay on this host')
        require(type(probe['port']) is int and 0 < probe['port'] < 65536 and probe['exchanges'], 'Invalid health probe')
        deadline = time.monotonic() + readiness_seconds
        while True:
            try:
                connection = socket.create_connection((probe['host'], probe['port']), timeout=2)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise ValueError('Local service did not become ready') from None
                time.sleep(0.2)
        with connection as stream:
            stream.settimeout(10)
            received = bytearray()
            for exchange in probe['exchanges']:
                if exchange.get('send'):
                    stream.sendall(exchange['send'].encode('utf-8'))
                expected = exchange['expect'].encode('utf-8')
                require(expected, 'Health check expectation must not be empty')
                deadline = time.monotonic() + 10
                while expected not in received and len(received) < 1024 * 1024 and time.monotonic() < deadline:
                    block = stream.recv(8192)
                    require(block, 'Health probe ended early')
                    received.extend(block)
                require(expected in received, 'Health probe did not match its expected response')
                # Preserve later prompts coalesced into the same TCP read.
                end = received.index(expected) + len(expected)
                del received[:end]


def verify_application(restored, profile, trusted):
    """Boot the old installed BBS against a disposable restored data copy.

    Never start Web/link/Door workers or point the verifier at live files. This
    checks restoration before update; it does not prove a future DB migration.
    """
    import pwd
    user = pwd.getpwnam(profile['run_user'])
    require(user.pw_uid != 0, 'Restore application must run unprivileged')
    executable = trusted(profile['executable'])
    source_root = Path(profile['data_root'])
    require(source_root.is_absolute(), 'Restore data root must be absolute')
    source = restored / str(source_root).lstrip('/')
    require(source.is_dir() and not source.is_symlink(), 'Restored application root is absent')
    temporary = Path(tempfile.mkdtemp(prefix='liner-restore-check-', dir='/var/tmp'))
    process = None
    try:
        data = temporary / 'data'
        shutil.copytree(source, data, symlinks=True)
        for path in [temporary, *temporary.rglob('*')]:
            require(not path.is_symlink() or path.resolve().is_relative_to(temporary), 'Restored application has an escaping symlink')
            os.chown(path, user.pw_uid, user.pw_gid, follow_symlinks=False)
        def mapped(value):
            original = Path(value)
            require(original.is_relative_to(source_root), 'Application restore field references a live/external path')
            return str(data / original.relative_to(source_root))
        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        argv = [str(executable), '--bind', '127.0.0.1',
                '--database-structured' if profile['structured'] else '--database', mapped(profile['database']),
                str(port), mapped(profile['board']), mapped(profile['members']), mapped(profile['catalog'])]
        process = subprocess.Popen(argv, cwd=data, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, start_new_session=True,
                                   user=user.pw_uid, group=user.pw_gid, extra_groups=[],
                                   env={'PATH': '/usr/bin:/bin:/usr/local/bin', 'HOME': str(data), 'LC_ALL': 'C'})
        require({'login', 'article', 'mail'} <= set(profile['probes']), 'Restore application probes are incomplete')
        probes = {name: dict(probe, host='127.0.0.1', port=port) for name, probe in profile['probes'].items()}
        health_probes(probes)
        require(process.poll() is None, 'Restored application terminated unexpectedly')
    finally:
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=10)
        shutil.rmtree(temporary)
