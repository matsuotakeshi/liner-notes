#!/usr/bin/env python3
"""Offline Liner notes backup, verification, restore and copy-only compaction.

Python 3.11+, standard library only. Stop the daemon AND transfer workers first.
The root must contain the complete instance, including custom configuration,
logs, public vault, private attachments and transfer spool. No symlinks.
"""
import argparse
import contextlib
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import sqlite3
import subprocess
import tarfile
import tempfile
import time

import notes_compact

FORMAT = 'liner-notes-backup-1'
MANIFEST = 'manifest.json'
INCOMPLETE = '.liner-maint-incomplete'
MAX_MANIFEST = 16 * 1024 * 1024
CHUNK = 1024 * 1024


def need(condition, message):
    if not condition:
        raise ValueError(message)


def relative(name):
    need(isinstance(name, str) and name and not any(ord(c) < 32 for c in name), 'Invalid relative path')
    need(not any(c in name for c in '\\:*?"<>|'), 'Non-portable path: ' + name)
    parts = name.split('/')
    need(not name.startswith('/') and all(p not in ('', '.', '..') for p in parts), 'Unsafe path: ' + name)
    # Also reject Windows aliases, so an archive has the same meaning on both OSes.
    need(all(not p.endswith((' ', '.')) and not re.fullmatch(r'(?i)(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?', p) for p in parts), 'Non-portable path: ' + name)
    need(parts[0] != INCOMPLETE, 'Incomplete maintenance directory; do not use it as live data')
    return name


def signature(p):
    s = p.lstat()
    need(not getattr(s, 'st_file_attributes', 0) & 0x400, 'Reparse points / junctions are not accepted: ' + str(p))
    return (s.st_dev, s.st_ino, s.st_mode, s.st_nlink, s.st_size, s.st_mtime_ns, s.st_ctime_ns)


def inventory(root, allow_incomplete=False):
    need(root.is_dir() and not root.is_symlink(), 'Root must be a real directory')
    need(allow_incomplete or not (root / INCOMPLETE).exists(), 'Root contains an incomplete maintenance result')
    files, directories, aliases = {}, [], set()
    def failed(error):
        raise error
    for base, dirs, names in os.walk(root, followlinks=False, onerror=failed):
        for name in sorted(dirs + names):
            if allow_incomplete and Path(base)==root and name==INCOMPLETE:
                continue
            p = Path(base) / name
            rel = relative(p.relative_to(root).as_posix())
            alias = rel.casefold()
            need(alias not in aliases, 'Case-insensitive path collision: ' + rel)
            aliases.add(alias)
            s = signature(p)
            need(not stat.S_ISLNK(s[2]), 'Symlinks are not accepted: ' + rel)
            if stat.S_ISDIR(s[2]):
                directories.append(rel)
            else:
                need(stat.S_ISREG(s[2]) and s[3] == 1, 'Special files / hard links are not accepted: ' + rel)
                files[rel] = s
    return dict(sorted(files.items())), sorted(directories)


def sync_directory(path):
    if os.name != 'nt':
        fd = os.open(path, os.O_RDONLY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def dump(value):
    return (json.dumps(value, ensure_ascii=True, sort_keys=True, indent=2) + '\n').encode()


class HashedReader:
    def __init__(self, stream):
        self.stream, self.digest = stream, hashlib.sha256()

    def read(self, size=-1):
        data = self.stream.read(size)
        self.digest.update(data)
        return data


def outside(path, root):
    resolved = path.resolve()
    need(resolved != root and root not in resolved.parents, 'Backup output must be outside the source root')


def backup(root, board, members, destination):
    root = Path(root).absolute()
    need(not root.is_symlink(), 'Root must not be a symlink')
    signature(root)
    root = root.resolve()
    destination = Path(destination).absolute()
    outside(destination, root)
    need(not destination.exists() and not destination.is_symlink(), 'Output already exists')
    files, directories = inventory(root)
    import sqlite3
    for name in files:
        if not name.endswith('.sqlite'):continue
        # Even mode=ro can create/change WAL shared-memory files. Inspect a
        # private copy (including WAL) so the probe never changes its source.
        with tempfile.TemporaryDirectory(prefix='.liner-sql-probe-',dir=destination.parent) as scratch:
            snapshot=Path(scratch)/'probe.sqlite'
            for suffix in ('','-wal'):
                if name+suffix not in files:continue
                fd=os.open(root/(name+suffix),os.O_RDONLY|getattr(os,'O_NOFOLLOW',0)|getattr(os,'O_BINARY',0))
                with os.fdopen(fd,'rb') as source,Path(str(snapshot)+suffix).open('wb') as target:
                    shutil.copyfileobj(source,target,CHUNK)
            with contextlib.closing(sqlite3.connect(snapshot.as_uri()+'?mode=ro',uri=True)) as db:
                if db.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='storage_meta'").fetchone():
                    raise ValueError('Authoritative SQLite database found; use db-backup with --database')
    board, members = relative(board), relative(members)
    need(board != members and board in files and members in files, 'Existing, separate board and members files are required')
    temporary = None
    try:
        with contextlib.ExitStack() as stack:
            # The runtime locks members before opening its sidecars. Holding both
            # anchors blocks a restart, as well as rejecting an already running BBS.
            # Read these through the SAME descriptors: on POSIX closing another
            # descriptor for that inode would release our process's record lock.
            anchors = [members, board]
            anchors += [n for n in files if n not in anchors and (n.endswith('.userlog.lock') or PurePosixPath(n).name == 'index.dat')]
            locked = {}
            for name in anchors:
                try:
                    locked[name] = stack.enter_context(notes_compact.locked(root / name))
                except OSError as error:
                    raise ValueError('Cannot lock ' + name + '; stop the BBS, transfer workers and other maintenance tools first') from error
            need(locked[members].read(8) == b'RTMEMB01', 'Not a members journal')
            board_header = locked[board].read(8)
            need(board_header in (b'RTBOARD1', b'RTBOARD2', b'RTBOARD3', b'RTBOARD4', b'RTBOARD5'), 'Not a board store')
            if board_header == b'RTBOARD5':
                need(all(board + s in files for s in ('.boardpolicy', '.boardmoves', '.boardaccess')), 'Missing required board sidecar')
            fd, temporary = tempfile.mkstemp(prefix='.liner-backup-', dir=destination.parent)
            manifest = dict(format=FORMAT, created=int(time.time()), board=board, members=members, directories=directories, files={})
            with os.fdopen(fd, 'w+b') as raw:
                with tarfile.open(fileobj=raw, mode='w:gz', format=tarfile.PAX_FORMAT) as archive:
                    for name, before in files.items():
                        with contextlib.ExitStack() as current:
                            stream = locked.get(name)
                            if stream is None:
                                fileno = os.open(root / name, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_BINARY', 0))
                                stream = current.enter_context(os.fdopen(fileno, 'rb'))
                            stream.seek(0)
                            need(signature(root / name) == before, 'Source changed: ' + name)
                            opened = os.fstat(stream.fileno())
                            need((opened.st_dev, opened.st_ino) == before[:2], 'Source replaced: ' + name)
                            reader = HashedReader(stream)
                            info = tarfile.TarInfo('data/' + name)
                            info.size, info.mtime = before[4], before[5] // 1000000000
                            info.mode = 0o700 if before[2] & 0o111 else 0o600
                            archive.addfile(info, reader)
                            manifest['files'][name] = dict(size=info.size, sha256=reader.digest.hexdigest(), mode=info.mode)
                    need(inventory(root) == (files, directories), 'Source changed during backup; no backup published')
                    encoded = dump(manifest)
                    need(len(encoded) <= MAX_MANIFEST, 'Backup inventory is too large')
                    info = tarfile.TarInfo(MANIFEST)
                    info.size, info.mode = len(encoded), 0o600
                    archive.addfile(info, io.BytesIO(encoded))
                raw.flush()
                os.fsync(raw.fileno())
            # Verify the actual compressed file before publishing it, exclusively.
            report = inspect_archive(temporary)
            os.link(temporary, destination)
            sync_directory(destination.parent)
            return dict(operation='backup', archive=str(destination), **report)
    finally:
        if temporary is not None:
            os.unlink(temporary)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        need(key not in result, 'Duplicate manifest key')
        result[key] = value
    return result


def manifest_valid(manifest, actual):
    need(isinstance(manifest, dict) and manifest.get('format') == FORMAT, 'Unsupported backup format')
    need(isinstance(manifest.get('files'), dict) and isinstance(manifest.get('directories'), list), 'Invalid manifest inventory')
    need(manifest['files'] == actual, 'Archive content / SHA256 does not match manifest')
    paths, aliases = set(), set()
    for name in list(manifest['files']) + manifest['directories']:
        relative(name)
        need(name not in paths and name.casefold() not in aliases, 'Duplicate or conflicting path')
        paths.add(name)
        aliases.add(name.casefold())
    dirs = set(manifest['directories'])
    for name in paths:
        parent = PurePosixPath(name).parent
        while str(parent) != '.':
            need(str(parent) in dirs, 'Missing directory or file/directory conflict')
            parent = parent.parent
    for field in ('board', 'members'):
        need(relative(manifest.get(field)) in actual, 'Missing required ' + field + ' file')
    need(manifest['board'] != manifest['members'], 'Board and members must differ')


def private_parents(path, root):
    relative(path.relative_to(root).as_posix())
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    # Intermediate mkdir(parents=True) permissions depend on umask.
    for parent in path.parents:
        if parent == root:
            break
        os.chmod(parent, 0o700)


def inspect_archive(path, output=None):
    actual, manifest, seen = {}, None, set()
    with open(path, 'rb') as raw:
        archive_hash = hashlib.file_digest(raw, 'sha256').hexdigest()
        raw.seek(0)
        with gzip.GzipFile(fileobj=raw, mode='rb') as compressed, tarfile.open(fileobj=compressed, mode='r|') as archive:
            for entry in archive:
                need(entry.name not in seen and entry.isfile() and not entry.sparse, 'Duplicate / unsupported archive entry')
                seen.add(entry.name)
                need(manifest is None, 'Manifest must be the last entry')
                stream = archive.extractfile(entry)
                if entry.name == MANIFEST:
                    need(entry.size <= MAX_MANIFEST, 'Manifest too large')
                    manifest = json.loads(stream.read(), object_pairs_hook=unique_object)
                    continue
                need(entry.name.startswith('data/'), 'Unexpected archive entry')
                name = relative(entry.name[5:])
                need(entry.mode in (0o600, 0o700) and entry.size >= 0, 'Invalid archive metadata')
                h, size = hashlib.sha256(), 0
                with contextlib.ExitStack() as stack:
                    out = None
                    if output is not None:
                        target = output / name
                        private_parents(target, output)
                        fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_BINARY', 0), 0o600)
                        out = stack.enter_context(os.fdopen(fd, 'wb'))
                    for chunk in iter(lambda: stream.read(CHUNK), b''):
                        h.update(chunk)
                        size += len(chunk)
                        if out:
                            out.write(chunk)
                    need(size == entry.size, 'Truncated file')
                    if out:
                        out.flush()
                        os.fsync(out.fileno())
                        os.chmod(output / name, entry.mode)
                actual[name] = dict(size=size, sha256=h.hexdigest(), mode=entry.mode)
            # Consume the footer too: TAR's end marker alone does not check gzip CRC.
            for chunk in iter(lambda: compressed.read(CHUNK), b''):
                need(not chunk.strip(b'\0'), 'Unexpected data after TAR end marker')
    manifest_valid(manifest, actual)
    if output is not None:
        for name in sorted(manifest['directories'], key=lambda n: (n.count('/'), n)):
            directory = output / name
            directory.mkdir(mode=0o700, exist_ok=True)
            os.chmod(directory, 0o700)
    return dict(files=len(actual), bytes=sum(r['size'] for r in actual.values()), archive_sha256=archive_hash, manifest=manifest)


def compact_copy(root, manifest, log_paths, keep_days, gather_count, reclaim_withdrawn=False):
    result = []
    caches = [manifest['board']+'.article-index.sqlite',manifest['board']+'.legacy-profile-index.sqlite',manifest['members']+'.accounts-index.sqlite',manifest['members']+'.read-index.sqlite',manifest['members']+'.profile-index.sqlite',manifest['members']+'.mail.mail-index.sqlite',manifest['members']+'.mail.draft-index.sqlite',manifest['members']+'.mail.attachment-index.sqlite']
    for name in caches:
        for suffix in ('','-journal','-wal','-shm'):
            path=root/(name+suffix)
            need(root.resolve() in path.resolve().parents,'Cache outside restored root')
            if path.exists():
                need(path.is_file() and not path.is_symlink(),'Unsafe index cache')
                size=path.stat().st_size;path.unlink();result.append(dict(path=name+suffix,operation='drop-rebuildable-index',source_bytes=size,output_bytes=0))
    with tempfile.TemporaryDirectory(prefix='.liner-compact-' , dir=root.parent) as temp:
        temp = Path(temp)
        for kind, name in [('board', manifest['board']), ('mail', manifest['members'] + '.mail')]:
            source = root / name
            if not source.exists() and kind == 'mail':
                continue
            destination = temp / kind
            report = notes_compact.compact(kind, source, destination, reclaim_withdrawn and kind=='board')
            os.replace(destination, source)
            notes_compact.compact(kind, source)  # Original move sidecar is beside this copy.
            report.pop('required_sidecar', None)
            result.append(dict(path=name, **report))
        spec = importlib.util.spec_from_file_location('log_retention', Path(__file__).with_name('log-retain.py'))
        retention = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(retention)
        for i, name in enumerate(log_paths):
            need(name in manifest['files'], 'Log not found in backup: ' + name)
            source, destination = root / name, temp / ('log-' + str(i))
            report = retention.run(source, destination, keep_days, gather_count)
            os.replace(destination / 'retained.dat', source)
            result.append(dict(path=name, **report))
    return result


def restore(archive, destination, compact=False, log_paths=(), keep_days=None, gather_count=16, all_stores=False, tools_dir=None, reclaim_withdrawn=False):
    destination = Path(destination).absolute()
    need(not destination.exists() and not destination.is_symlink(), 'Restore output must not exist')
    need(keep_days is None or (type(keep_days) is int and 0 <= keep_days <= 36500), 'keep-log-days must be 0..36500')
    need(0 <= gather_count <= 1000, 'gather-count must be 0..1000')
    need(bool(log_paths) == (keep_days is not None), 'Specify --log and --keep-log-days together')
    for name in log_paths:
        relative(name)
    need(len(set(log_paths)) == len(log_paths), 'Duplicate log path')
    report = inspect_archive(archive)
    destination.mkdir(mode=0o700)  # Exclusive reservation: never replace an existing directory.
    marker = destination / INCOMPLETE
    marker.write_text('INCOMPLETE: do not start the BBS in this directory.\n', encoding='ascii')
    os.chmod(marker, 0o600)
    try:
        extracted = inspect_archive(archive, destination)
        need(extracted == report, 'Backup changed during restore')
        changes = compact_copy(destination, report['manifest'], log_paths, keep_days, gather_count, reclaim_withdrawn) if compact else []
        if all_stores:
            need(compact, '--all-stores requires compact')
            from state_compact import compact_all
            changes += compact_all(destination, report['manifest'], tools_dir or os.environ.get('LINER_NOTES_TOOLS_DIR') or Path(__file__).resolve().parents[1])
        for base, dirs, files in os.walk(destination, topdown=False):
            sync_directory(base)
        marker.unlink()
        sync_directory(destination)
        sync_directory(destination.parent)
    except Exception as error:
        raise ValueError('Incomplete output retained at ' + str(destination) + '; do not start it: ' + str(error)) from error
    return dict(operation='compact' if compact else 'restore', directory=str(destination), changes=changes, **report)


def main():
    import sys
    if len(sys.argv)>1 and sys.argv[1]=='upgrade-check':
        import upgrade_check
        return upgrade_check.main(sys.argv[2:])
    if len(sys.argv)>1 and sys.argv[1]=='transfers':
        import transfer_spool
        return transfer_spool.main(sys.argv[2:])
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='operation', required=True)
    p = sub.add_parser('backup', help='Back up an entire stopped instance; output must be outside root')
    p.add_argument('--root', required=True)
    p.add_argument('--board', required=True, help='Board file relative to root')
    p.add_argument('--members', required=True, help='Members file relative to root')
    p.add_argument('archive')
    sub.add_parser('verify', help='Check archive inventory and every file SHA256').add_argument('archive')
    for op in ('restore', 'compact'):
        p = sub.add_parser(op, help='Create a new directory; preserve the backup and original data')
        p.add_argument('archive')
        p.add_argument('directory')
        if op == 'compact':
            p.add_argument('--log', action='append', default=[], help='Relative log path; repeat to prune multiple logs')
            p.add_argument('--keep-log-days', type=int, help='0 keeps all dates; requires --log')
            p.add_argument('--gather-count', type=int, default=16)
            p.add_argument('--reclaim-withdrawn', action='store_true', help='Remove withdrawn article bodies in the new copy; preserve IDs and backup')
            p.add_argument('--all-stores', action='store_true', help='Also snapshot members, profiles and vault state; keep prior history in backup')
            p.add_argument('--tools-dir', help='Directory containing memberctl and vaultctl (default: installation root)')
    p=sub.add_parser('db-backup',help='Logical backup of a stopped SQLite instance and file payloads')
    p.add_argument('--root',required=True);p.add_argument('--database',required=True);p.add_argument('--board',required=True);p.add_argument('--members',required=True);p.add_argument('archive')
    p=sub.add_parser('db-export',help='Export SQLite records to a new portable legacy copy')
    p.add_argument('--root',required=True);p.add_argument('--database',required=True);p.add_argument('--board',required=True);p.add_argument('directory')
    p=sub.add_parser('db-migrate',help='Copy a legacy instance and migrate its records to SQLite')
    p.add_argument('--root',required=True);p.add_argument('--board',required=True);p.add_argument('--members',required=True);p.add_argument('--database',default='liner.sqlite');p.add_argument('directory')
    p.add_argument('--structured',action='store_true',default=None);p.add_argument('--tools-dir')
    p=sub.add_parser('db-structure',help='Convert a stopped SQLite instance into a NEW structured copy, verifying every stream')
    p.add_argument('--root',required=True);p.add_argument('--database',required=True);p.add_argument('--board',required=True);p.add_argument('--members',required=True);p.add_argument('--tools-dir');p.add_argument('directory')
    for operation in ('db-restore','db-compact'):
        p=sub.add_parser(operation,help='Restore a new SQLite instance from a logical backup')
        p.add_argument('archive');p.add_argument('directory');p.add_argument('--database',default='liner.sqlite')
        p.add_argument('--structured',action='store_true',default=None);p.add_argument('--tools-dir')
        if operation=='db-compact':
            p.add_argument('--reclaim-withdrawn',action='store_true');p.add_argument('--all-stores',action='store_true')
    args = parser.parse_args()
    try:
        if args.operation.startswith('db-'):
            import database_tools as database
            if args.operation=='db-backup':result=database.backup(args.root,args.database,args.board,args.members,args.archive)
            elif args.operation=='db-structure':result=database.structure(args.root,args.database,args.board,args.members,args.directory,args.tools_dir)
            elif args.operation=='db-export':result=database.export_instance(args.root,args.database,args.board,args.directory)
            elif args.operation=='db-migrate':
                source=Path(args.root).resolve();destination=Path(args.directory).absolute();outside(destination,source)
                # Existing backup validation and locks also guard initial migration.
                with tempfile.TemporaryDirectory(prefix='liner-migrate-',dir=destination.parent) as tmp:
                    archive=Path(tmp)/'source.tar.gz';backup(source,args.board,args.members,archive)
                    result=database.restore(archive,destination,database=args.database,structured=args.structured,tools_dir=args.tools_dir)
            else:result=database.restore(args.archive,args.directory,args.operation=='db-compact',getattr(args,'reclaim_withdrawn',False),getattr(args,'all_stores',False),args.tools_dir,args.database,args.structured)
        elif args.operation == 'backup':
            result = backup(args.root, args.board, args.members, args.archive)
        elif args.operation == 'verify':
            result = dict(operation='verify', **inspect_archive(args.archive))
        else:
            result = restore(args.archive, args.directory, args.operation == 'compact', getattr(args, 'log', ()), getattr(args, 'keep_log_days', None), getattr(args, 'gather_count', 16), getattr(args, 'all_stores', False), getattr(args, 'tools_dir', None), getattr(args, 'reclaim_withdrawn', False))
        result.pop('manifest', None)
        print(json.dumps(result, ensure_ascii=True, indent=2))
    except (OSError, ValueError, tarfile.TarError, EOFError, sqlite3.Error, subprocess.CalledProcessError) as error:
        parser.exit(1, 'Maintenance failed: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
