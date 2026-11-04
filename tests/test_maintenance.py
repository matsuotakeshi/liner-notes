"""Offline archive safety, original preservation and real runtime round trips."""
import contextlib
import shutil
import hashlib
import io
import json
import os
import sqlite3
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import maintenance as tool


def hashes(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}


def rejected(call):
    try:
        call()
    except (ValueError, OSError, tarfile.TarError, EOFError):
        return
    raise AssertionError('unsafe operation accepted')


def altered(source, target, mutate):
    with tarfile.open(source, 'r:gz') as incoming, tarfile.open(target, 'w:gz') as outgoing:
        for entry in incoming:
            content = incoming.extractfile(entry).read()
            entry, content = mutate(entry, content)
            entry.size = len(content)
            outgoing.addfile(entry, io.BytesIO(content))


def unit():
    with tempfile.TemporaryDirectory(prefix='maintenance-unit-') as directory:
        p = Path(directory); root = p / 'source'; root.mkdir()
        (root / 'board').write_bytes(b'RTBOARD4')
        (root / 'members').write_bytes(b'RTMEMB01')
        (root / 'members.mail').write_bytes(b'LNMAIL01')
        (root / 'private').mkdir(); (root / 'private' / 'empty').mkdir()
        (root / 'private' / '日本語.bin').write_bytes(bytes(range(256)) * 12345)
        (root / 'launcher').write_bytes(b'#!/bin/sh\nexit 0\n'); os.chmod(root / 'launcher', 0o700)
        archive = p / 'backup.tar.gz'; before = hashes(root)
        rejected(lambda: tool.backup(root, 'board', 'members', root / 'bad.tar.gz'))
        assert not (root / 'bad.tar.gz').exists()
        result = tool.backup(root, 'board', 'members', archive)
        assert result['files'] == 5 and hashes(root) == before
        assert tool.inspect_archive(archive)['manifest']['files']['private/日本語.bin']['size'] > tool.CHUNK
        if os.name == 'posix':
            assert archive.stat().st_mode & 0o777 == 0o600
        rejected(lambda: tool.backup(root, 'board', 'members', archive))
        tool.restore(archive, p / 'restored'); assert hashes(p / 'restored') == before
        assert (p / 'restored' / 'private' / 'empty').is_dir()
        if os.name == 'posix':
            assert (p / 'restored' / 'launcher').stat().st_mode & 0o777 == 0o700
            assert (p / 'restored' / 'private').stat().st_mode & 0o777 == 0o700
        rejected(lambda: tool.restore(archive, p / 'restored'))
        result = tool.restore(archive, p / 'compacted', compact=True)
        assert len(result['changes']) == 2 and hashes(root) == before
        for args in [
            ['backup', '--root', str(root), '--board', 'board', '--members', 'members', str(p / 'cli.tar.gz')],
            ['verify', str(archive)],
            ['restore', str(archive), str(p / 'cli-restored')],
            ['compact', str(archive), str(p / 'cli-compacted')],
        ]:
            cli = subprocess.run([sys.executable, str(ROOT / 'scripts/maintenance.py')] + args, capture_output=True, text=True, timeout=30)
            assert cli.returncode == 0, cli.stderr
            assert json.loads(cli.stdout)['operation'] == args[0]
            assert 'manifest' not in json.loads(cli.stdout)
        cli = subprocess.run([sys.executable, str(ROOT / 'scripts/maintenance.py'), 'restore', str(archive), str(root)], capture_output=True, text=True, timeout=30)
        assert cli.returncode == 1 and hashes(root) == before
        rejected(lambda: tool.restore(archive, p / 'bad-option', compact=True, keep_days=1))
        assert not (p / 'bad-option').exists()
        rejected(lambda: tool.restore(archive, p / 'bad-log', compact=True, log_paths=['../secret'], keep_days=1))
        # Failed compaction leaves a conspicuous marker and keeps archive/original.
        rejected(lambda: tool.restore(archive, p / 'incomplete', compact=True, log_paths=['members'], keep_days=1))
        assert (p / 'incomplete' / tool.INCOMPLETE).exists()
        rejected(lambda: tool.backup(p / 'incomplete', 'board', 'members', p / 'bad.tar.gz'))
        assert hashes(root) == before

        def corrupt(entry, data):
            return entry, data + b'corrupt' if entry.name == 'data/board' else data
        altered(archive, p / 'corrupt.tar.gz', corrupt)
        rejected(lambda: tool.restore(p / 'corrupt.tar.gz', p / 'bad-restore'))
        assert not (p / 'bad-restore').exists()
        def traverse(entry, data):
            if entry.name == 'data/board': entry.name = 'data/../escape'
            return entry, data
        altered(archive, p / 'traverse.tar.gz', traverse)
        rejected(lambda: tool.restore(p / 'traverse.tar.gz', p / 'traversed'))
        assert not (p / 'escape').exists()
        def link(entry, data):
            if entry.name == 'data/board': entry.type = tarfile.SYMTYPE; entry.linkname = '/etc/passwd'
            return entry, data
        altered(archive, p / 'link.tar.gz', link)
        rejected(lambda: tool.inspect_archive(p / 'link.tar.gz'))
        def duplicate(entry, data):
            if entry.name == 'data/members': entry.name = 'data/board'
            return entry, data
        altered(archive, p / 'duplicate.tar.gz', duplicate)
        rejected(lambda: tool.inspect_archive(p / 'duplicate.tar.gz'))
        raw = archive.read_bytes()
        for name, data in [('truncated', raw[:-5]), ('crc', raw[:-8] + bytes([raw[-8] ^ 1]) + raw[-7:])]:
            bad = p / (name + '.tar.gz'); bad.write_bytes(data)
            rejected(lambda: tool.inspect_archive(bad))
        # The daemon's fcntl / Windows byte-range lock must be respected.
        process = subprocess.Popen([sys.executable, '-c',
            'import sys,pathlib;sys.path.insert(0,sys.argv[1]);import notes_compact;'
            'c=notes_compact.locked(pathlib.Path(sys.argv[2]));c.__enter__();'
            'print("LOCKED",flush=True);sys.stdin.readline();c.__exit__(None,None,None)',
            str(ROOT / 'scripts'), str(root / 'members')], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            assert process.stdout.readline().strip() == 'LOCKED'
            rejected(lambda: tool.backup(root, 'board', 'members', p / 'busy.tar.gz'))
            assert not (p / 'busy.tar.gz').exists()
        finally:
            process.communicate('\n', timeout=10)
        # A file written after it was copied cannot produce a successful backup.
        original_add = tarfile.TarFile.addfile
        def changed(archive_obj, entry, fileobj=None):
            original_add(archive_obj, entry, fileobj)
            if entry.name == 'data/board': (root / 'board').write_bytes(b'RTBOARD4changed')
        with patch.object(tarfile.TarFile, 'addfile', changed):
            rejected(lambda: tool.backup(root, 'board', 'members', p / 'changed.tar.gz'))
        assert not (p / 'changed.tar.gz').exists()
        (root / 'board').write_bytes(b'RTBOARD4')
        if os.name == 'posix':
            (root / 'alias').symlink_to(root / 'board')
            rejected(lambda: tool.backup(root, 'board', 'members', p / 'symlink.tar.gz'))
            (root / 'alias').unlink()
        os.link(root / 'board', root / 'hardlink')
        rejected(lambda: tool.backup(root, 'board', 'members', p / 'hardlink.tar.gz'))
        print('PASS maintenance: hashes, corruption/truncation/CRC, traversal, links, locks, changed source, exclusive output, copy-only failures')


def integration(binary, ctl, memberctl=None):
    from test_board import Server, post, read
    from test_runtime import Client
    from test_members import auth, PASSWORD
    from test_mail import send, read as mail_read
    from test_stage58 import cmd, rows
    from transfer_connectors import MailConnector, RemoteError
    binary, ctl = str(Path(binary).resolve()), str(Path(ctl).resolve())
    with tempfile.TemporaryDirectory(prefix='maintenance-runtime-') as directory:
        p = Path(directory); root = p / 'live'; root.mkdir()
        vault = root / 'vault'
        subprocess.run([ctl, 'init', str(vault)], check=True, capture_output=True)
        payload = b'private attachment\0\xff' * 20
        source = p / 'public.txt'; source.write_bytes(b'public download\r\n')
        subprocess.run([ctl, 'import', str(vault), str(source), 'public.txt', 'description'], check=True, capture_output=True)
        (root / 'empty-spool').mkdir(); (root / 'banner.txt').write_text('Liner notes\n')
        def start(where, admin=True):
            return Server(binary, where / 'notes', members=where / 'members', vault=where / 'vault', admins=where / 'admins' if admin else None)
        server = start(root, False)
        try:
            for name in ('admin', 'alice', 'bob'):
                c = Client(server.port); auth(c, name, True); c.close()
        finally: server.close()
        (root / 'admins').write_text('1\n')
        server = start(root); a = Client(server.port); b = Client(server.port); admin = Client(server.port)
        connector = None
        try:
            auth(a, 'alice'); auth(b, 'bob'); auth(admin, 'admin')
            assert post(a, 'original', 'original body') == 1
            assert post(a, 'reply', 'reply body', 1) == 2
            assert cmd(admin, 'BOARDSET 2|1|all|member|Moved|guide') == b'BOARD UPDATED 2'
            assert cmd(admin, 'MOVE 1 2 CONFIRM') == b'THREAD MOVED 1 2'
            assert cmd(a, 'BOARD 2').startswith(b'BOARD 2')
            assert post(a, 'after move', 'moved reply', 2) == 3
            assert cmd(a, 'PROFILESET REALNAME Private Person') == b'PROFILE SAVED'
            assert cmd(a, 'GROUP 2 100') == b'ERR admin required'
            assert cmd(admin, 'GROUP 2 100').startswith(b'RIGHTS 2')
            a.close(); a = Client(server.port); auth(a, 'alice')
            connector = MailConnector('127.0.0.1', server.port, 'alice', PASSWORD)
            ident = connector.begin(len(payload), hashlib.sha256(payload).hexdigest(), 'private.bin', 'private')
            connector.write(ident, 0, payload); connector.finish(ident)
            assert cmd(a, 'MAIL SEND bob attachment').startswith(b'MAIL EDIT')
            assert cmd(a, f'/ATTACH {ident}').startswith(b'MAIL ATTACHMENT')
            a.send(b'private mail body\n.\n'); assert a.line() == b'MAIL SENT 1'
            assert send(a, 'bob', 'deleted', 'deleted mail secret') == 2
            assert cmd(a, 'MAIL DELETE 2').startswith(b'MAIL DELETE CONFIRM')
            assert cmd(a, 'Y') == b'MAIL DELETED 2'
            assert cmd(a, 'MAIL SEND bob draft').startswith(b'MAIL EDIT')
            a.send(b'draft body\n'); assert cmd(a, '/SAVE') == b'MAIL DRAFT SAVED'
            assert cmd(a, 'MAIL 0') == b'MAIL CLOSED'
            rejected(lambda: tool.backup(root, 'notes', 'members', p / 'busy.tar.gz'))
            assert not (p / 'busy.tar.gz').exists()
        finally:
            if connector: connector.close()
            a.close(); b.close(); admin.close(); server.close()
        before = hashes(root); archive = p / 'backup.tar.gz'
        original_add = tarfile.TarFile.addfile
        attempted = []
        def restart_during_backup(archive_obj, entry, fileobj=None):
            original_add(archive_obj, entry, fileobj)
            if not attempted:
                attempted.append(True)
                blocked = subprocess.run([binary, '0', str(root / 'notes'), str(root / 'members')], capture_output=True, timeout=10)
                assert blocked.returncode != 0 and b'members: cannot open/lock/validate' in blocked.stderr
        with patch.object(tarfile.TarFile, 'addfile', restart_during_backup):
            tool.backup(root, 'notes', 'members', archive)
        assert attempted
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        tool.restore(archive, p / 'restored')
        assert hashes(p / 'restored') == before
        result = tool.restore(archive, p / 'compacted', compact=True,
            log_paths=['members.userlog', 'notes.activity', 'notes.boardaccess'], keep_days=90)
        assert len([c for c in result['changes'] if c.get('operation')!='drop-rebuildable-index']) == 5
        assert b'deleted mail secret' not in (p / 'compacted' / 'members.mail').read_bytes()
        assert any(item.get('saved_bytes', 0) > 0 for item in result['changes'])
        assert hashes(root) == before and hashlib.sha256(archive.read_bytes()).hexdigest() == digest
        labels = ['restored', 'compacted']
        if memberctl:
            helpers=p/'helpers';helpers.mkdir()
            for name,helper_source in [('memberctl',memberctl),('vaultctl',ctl)]:
                shutil.copy2(helper_source,helpers/(name+('.exe' if os.name=='nt' else '')))
            tool.restore(archive,p/'all-compacted',compact=True,all_stores=True,tools_dir=helpers,
                log_paths=['members.userlog','notes.activity','notes.boardaccess'],keep_days=90)
            labels.append('all-compacted')
        for label in labels:
            target = p / label
            assert (target / 'empty-spool').is_dir()
            assert (target / 'vault' / '00000001.bin').read_bytes() == source.read_bytes()
            assert (target / 'members.mailfiles' / f'{ident:08}.bin').read_bytes() == payload
            server = start(target); a = Client(server.port); b = Client(server.port); admin = Client(server.port)
            ca = cb = cc = None
            try:
                auth(a, 'alice'); auth(b, 'bob'); auth(admin, 'admin')
                assert cmd(a, 'BOARD 2').startswith(b'BOARD 2')
                assert read(a, 1)[2] == 'original body\r\n'
                assert read(a, 3)[0] == 2
                assert post(a, 'new', 'still monotonic', 3) == 4
                assert b'private mail body' in mail_read(b, 1)
                assert mail_read(b, 2) == b'MAIL ERROR unavailable'
                assert cmd(a, 'MAIL RESUME').startswith(b'MAIL DRAFT')
                assert cmd(a, '.') == b'MAIL SENT 3'
                assert b'Private Person' not in b'\n'.join(rows(b, 'PROFILEID 2', b'END PROFILE'))
                assert b'Private Person' in b'\n'.join(rows(a, 'PROFILEID 2', b'END PROFILE'))
                assert any(x.startswith(b'2 ') for x in rows(admin, 'PROFILEFIND GROUP 100'))
                ca, cb, cc = [MailConnector('127.0.0.1', server.port, name, PASSWORD) for name in ('alice', 'bob', 'admin')]
                assert cb.read(ident, 0) == payload
                try: cc.read(ident, 0)
                except RemoteError: pass
                else: raise AssertionError('SYSOP gained private attachment access')
                new_id = ca.begin(0, hashlib.sha256(b'').hexdigest(), 'new.bin', '')
                assert new_id > ident
            finally:
                for conn in (ca, cb, cc):
                    if conn: conn.close()
                a.close(); b.close(); admin.close(); server.close()
        assert hashes(root) == before
        print('PASS maintenance runtime: restore + compact restart, moved replies, monotonic IDs, deleted mail, draft, group100, private profile/attachment, public vault, original unchanged')


def sqlite_probe():
    with tempfile.TemporaryDirectory(prefix='maintenance-sql-probe-') as tmp:
        p=Path(tmp);root=p/'source';root.mkdir()
        (root/'board').write_bytes(b'RTBOARD4');(root/'members').write_bytes(b'RTMEMB01')
        db=sqlite3.connect(root/'extra.sqlite');db.execute('PRAGMA journal_mode=WAL');db.execute('CREATE TABLE data(value)');db.commit();db.close()
        before=hashes(root);tool.backup(root,'board','members',p/'closed.tar.gz');assert hashes(root)==before
        # Detect storage_meta even if the schema exists only in the WAL, and
        # do not mutate the source SHM while inspecting it.
        db=sqlite3.connect(root/'authoritative.sqlite');db.execute('PRAGMA journal_mode=WAL');db.execute('CREATE TABLE storage_meta(version)');db.commit()
        try:
            before=hashes(root);rejected(lambda:tool.backup(root,'board','members',p/'unsafe.tar.gz'));assert hashes(root)==before
            assert not (p/'unsafe.tar.gz').exists()
        finally:db.close()
    print('PASS SQLite backup probe: closed WAL source unchanged; authoritative WAL detected without source SHM mutation')


if __name__ == '__main__':
    sqlite_probe()
    unit()
    if len(sys.argv) > 1:
        integration(*sys.argv[1:])
