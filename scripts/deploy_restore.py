"""Read-only verification of backup copies and configured local health probes."""
import hashlib
import contextlib
import os
from pathlib import Path
import socket
import sqlite3
import shutil
import signal
import stat
import subprocess
import tempfile
import time
from deploy_policy import digest_file, require


def snapshot_rows(source, query):
    """Query a private DB/WAL copy; even a SQLite read-only source can create SHM.

    The caller has already stopped services. A changing file or rollback journal
    rejects the backup instead of letting SQLite recover or lock the original.
    """
    source = Path(source)
    paths = [source, Path(str(source) + '-wal'), Path(str(source) + '-journal')]
    def identity(info):
        return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns,
                info.st_ctime_ns if os.name != 'nt' else None)
    def metadata():
        values = {}
        for path in paths:
            try:
                info = path.lstat()
            except FileNotFoundError:
                values[path] = None
                continue
            require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1,
                    'Unsafe database snapshot source')
            values[path] = identity(info)
        return values
    require(isinstance(query, str) and query.lstrip().upper().startswith('SELECT '),
            'Restore verification requires SELECT')
    before = metadata()
    require(before[source] is not None, 'Database snapshot source is missing')
    require(before[paths[2]] is None or before[paths[2]][2] == 0,
            'Stopped database has a rollback journal; recovery must be planned')
    total = sum(item[2] for item in before.values() if item)
    deadline = time.monotonic() + 120
    with tempfile.TemporaryDirectory(prefix='liner-restore-sql-') as temporary:
        os.chmod(temporary, 0o700)
        require(shutil.disk_usage(temporary).free >= total * 2 + 16 * 1024**2,
                'Insufficient space for private database verification')
        copy = Path(temporary) / 'database.sqlite'
        for path in paths[:2]:
            if before[path] is None:
                continue
            flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0)
            with os.fdopen(os.open(path, flags), 'rb') as incoming:
                info = os.fstat(incoming.fileno())
                require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and identity(info) == before[path],
                        'Database changed before private copy')
                destination = copy if path == source else Path(str(copy) + '-wal')
                with destination.open('xb') as outgoing:
                    os.chmod(destination, 0o600)
                    remaining = before[path][2]
                    while remaining:
                        require(time.monotonic() < deadline, 'Database snapshot exceeded its time budget')
                        block = incoming.read(min(remaining, 1024 * 1024))
                        require(block, 'Database changed during private copy')
                        outgoing.write(block)
                        remaining -= len(block)
        require(metadata() == before, 'Database changed during private copy')
        with contextlib.closing(sqlite3.connect(copy.as_uri() + '?mode=ro', uri=True, timeout=0)) as db:
            db.execute('PRAGMA query_only=ON')
            db.execute('PRAGMA trusted_schema=OFF')
            db.set_progress_handler(lambda: int(time.monotonic() >= deadline), 1000)
            rows = db.execute(query).fetchall()
        require(metadata() == before, 'Database changed during verification')
        return rows


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
        require(snapshot_rows(source, check['query']) == snapshot_rows(copy, check['query']),
                'Restored application records differ')


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
        catalog = None
        if profile.get('catalog'):
            original = Path(profile['catalog'])
            require(original.is_absolute(), 'Restore catalog must have an absolute source path')
            if original.is_relative_to(source_root):
                catalog = data / original.relative_to(source_root)
            else:
                copied_config = restored / str(original).lstrip('/')
                require(copied_config.is_file() and not copied_config.is_symlink(), 'Catalog was not included in the verified backup')
                catalog = temporary / 'catalog.conf'
                shutil.copyfile(copied_config, catalog)
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
                str(port), mapped(profile['board']), mapped(profile['members'])]
        if catalog is not None:
            argv.append(str(catalog))
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
