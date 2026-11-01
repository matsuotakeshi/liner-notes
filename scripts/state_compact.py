"""State-aware offline compaction of a freshly restored maintenance copy.

The full backup is the retained history. Never use this module on live data.
"""
import hashlib
import datetime
import importlib.util
import os
import re
from pathlib import Path
import struct
import subprocess
import tempfile

from notes_compact import need, exact, u32, put, sealed, valid_checksum


def retention_module():
    spec = importlib.util.spec_from_file_location('retention', Path(__file__).with_name('log-retain.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def member_states(path):
    deleted, ids = set(), set()
    with path.open('rb') as f:
        need(exact(f, 8) == b'RTMEMB01', 'Unsupported member store')
        while prefix := f.read(128):
            need(len(prefix) == 128, 'Truncated member row')
            size = 1152 if prefix[0] == ord('D') else max(128, 116 + u32(prefix, 24)) if prefix[0] == ord('d') else 128
            need(size <= 262144 + 128, 'Invalid member row')
            row = prefix + exact(f, size - 128)
            valid_checksum(row)
            if row[0] in b'AJ': ids.add(u32(row, 4))
            if row[0] == ord('X'): deleted.add(u32(row, 4))
    return ids, deleted


def profile_copy(source, members):
    retention = retention_module()
    magic, rows = retention.read_rows(source.read_bytes(), {b'LNPROF01'})
    ids, deleted = member_states(members)
    values, visibility, pending = {}, {}, None
    for row in rows:
        user = row['target']
        need(user in ids and row['actor'] in ids, 'Invalid profile member')
        if pending:
            owner, field, size, text = pending
            need(user == owner, 'Interleaved profile transaction')
            if row['kind'] == 'J':
                text += row['text']
                need(row['text'] and len(text.encode()) <= size, 'Invalid profile fragment')
                pending = (owner, field, size, text)
            else:
                need(row['kind'] == 'E' and not row['text'] and len(text.encode()) == size, 'Invalid profile commit')
                need(not any(ord(c) < 32 and not (field in (1, 13) and c in '\r\n\t') or 127 <= ord(c) <= 159 for c in text), 'Invalid profile text')
                if field == 8 and text:
                    need(len(text) == 10 and text[:4].isascii() and text[:4].isdigit() and int(text[:4]) >= 1900, 'Invalid birthday')
                    need(datetime.date.fromisoformat(text).isoformat() == text, 'Invalid birthday')
                values[user, field] = text
                pending = None
        else:
            parts = row['text'].split(' ')
            need(len(parts) == 2 and all(s.isascii() and s.isdigit() for s in parts), 'Invalid profile header')
            field, value = map(int, parts)
            need(row['text'] == f'{field} {value}', 'Noncanonical profile header')
            if row['kind'] == 'V':
                need(field < 13 and value <= 2 and (field != 0 or value == 1) and (value != 2 or field == 8), 'Invalid visibility')
                visibility[user, field] = value
            else:
                need(row['kind'] == 'I' and field < 14 and value <= (262144 if field == 13 else 8192 if field == 1 else 80 if field == 0 else 320), 'Invalid profile size')
                pending = (user, field, value, '')
    need(pending is None, 'Incomplete profile transaction')
    output = []
    def add(kind, user, text):
        output.append(dict(kind=kind, actor=user, target=user, time=0, text=text))
    for (user, field), text in sorted(values.items()):
        if user in deleted: continue
        add('I', user, f'{field} {len(text.encode())}')
        # <= 500 Unicode scalars => <= 2000 UTF-8 bytes per fragment.
        for at in range(0, len(text), 500): add('J', user, text[at:at+500])
        add('E', user, '')
    for (user, field), value in sorted(visibility.items()):
        if user not in deleted: add('V', user, f'{field} {value}')
    if rows and not output:
        # Preserve the mandatory non-empty profile sidecar marker.
        user = min(ids)
        add('I', user, '0 0'); add('E', user, '')
    return retention.encode(magic, output)


def moderation_copy(source, members):
    ids, _ = member_states(members)
    data = source.read_bytes()
    need(data[:8] == b'RTMOD001' and (len(data)-8) % 32 == 0, 'Invalid moderation journal')
    rows, latest = [], {}
    for at in range(8, len(data), 32):
        row = data[at:at+32]; valid_checksum(row)
        need(u32(row, 4) == len(rows)+1 and u32(row, 16) in ids and row[0] in b'MSK' and row[1] <= 1 and not any(row[2:4]), 'Invalid moderation record')
        need(0 < u32(row, 8) <= 2147483646 and (u32(row, 20) | u32(row, 24) << 32) <= 253402300799, 'Invalid moderation target/time')
        if row[0] == ord('M'):
            need(1 <= u32(row, 12) <= 1024, 'Invalid moderation board')
        else:
            need(u32(row, 8) in ids and u32(row, 8) != u32(row, 16) and not u32(row, 12) and (row[0] != ord('K') or row[1] == 1), 'Invalid moderation member')
        latest[row[0], u32(row, 8)] = len(rows)
        rows.append(row)
    mapping, kept = {0: 0}, []
    for i, row in enumerate(rows):
        if latest[row[0], u32(row, 8)] == i:
            row = bytearray(row[:-4]); put(row, 4, len(kept)+1); kept.append(sealed(row))
        mapping[i+1] = len(kept)
    return b'RTMOD001' + b''.join(kept), mapping


def extras_copy(source, members, moderation_map=None):
    ids, deleted = member_states(members)
    data = source.read_bytes()
    need(data[:8] == b'RTEXTRA1' and (len(data)-8) % 512 == 0, 'Invalid extras journal')
    rows, latest, removed, previous_anchor = [], {}, set(), 0
    for at in range(8, len(data), 512):
        row = data[at:at+512]; valid_checksum(row)
        need(u32(row, 4) == len(rows)+1 and u32(row, 12) in ids, 'Invalid extras sequence/member')
        kind, user = chr(row[0]), u32(row, 8)
        need(kind in 'DATHBROIWK', 'Unknown extras record')
        length = u32(row, 16)
        need(row[1] <= 1 and not any(row[2:4]) and (not row[1] or kind in 'DAT') and length <= 320, 'Invalid extras header')
        text = row[20:20+length]
        need(not any(c < 32 or c == 127 for c in text), 'Invalid extras text')
        need(not any(row[i] for i in range(20+length, 508) if not row[1] or i < 340 or i >= 352), 'Invalid extras padding')
        if kind not in 'DAT':
            need(user == u32(row, 12), 'Invalid profile actor')
            need(kind != 'H' or length <= 80, 'Invalid handle length')
        else:
            need(0 < user <= 2147483646 and (length == 0 if kind == 'D' else 0 < length <= 80 and user not in removed), 'Invalid article change')
            if kind == 'D': removed.add(user)
            anchor = u32(row, 348) if row[1] else 0
            need(anchor >= previous_anchor and (moderation_map is None or anchor in moderation_map), 'Invalid audit ordering')
            previous_anchor = anchor
        latest[kind, user] = len(rows)
        rows.append(row)
    kept = []
    for index, row in enumerate(rows):
        kind, user = chr(row[0]), u32(row, 8)
        if latest[kind, user] != index or (kind not in 'DAT' and user in deleted): continue
        row = bytearray(row[:-4]); put(row, 4, len(kept)+1)
        if kind in 'DAT' and row[1] and moderation_map is not None:
            old = u32(row, 348)
            need(old in moderation_map, 'Unknown moderation audit anchor')
            put(row, 348, moderation_map[old])
        kept.append(sealed(row))
    return b'RTEXTRA1' + b''.join(kept)


def policy_copy(source, members=None):
    retention = retention_module()
    magic, rows = retention.read_rows(source.read_bytes(), {b'LNBDP001', b'LNSIG001'})
    latest, pending, deleted = {}, None, set()
    ids = member_states(members)[0] if members is not None else set(range(1, 10000))
    for index, row in enumerate(rows):
        need(1 <= row['target'] <= 1024 and 1 <= row['actor'] <= 999999, 'Invalid board policy actor/board')
        need(row['actor'] in ids, 'Unknown board policy actor')
        if magic == b'LNSIG001':
            need(row['kind'] == 'S' and row['text'].isascii() and row['text'].isdigit() and int(row['text']) <= 999999, 'Invalid SIGOP')
            need(row['text'] == str(int(row['text'])) and (not int(row['text']) or int(row['text']) in ids), 'Unknown SIGOP')
            latest['S', row['target']] = [index]
        elif pending:
            first, indexes, text, size = pending
            need(row['target'] == first['target'] and row['actor'] == first['actor'], 'Interleaved board guide')
            indexes.append(index)
            if row['kind'] == 'J':
                text += row['text']; need(row['text'] and len(text.encode()) <= size, 'Invalid board guide fragment')
                pending = first, indexes, text, size
            else:
                need(row['kind'] == 'E' and not row['text'] and len(text.encode()) == size, 'Invalid board guide commit')
                need(not any(ord(c) < 32 and c not in '\r\n\t' or ord(c) == 127 for c in text), 'Invalid board guide text')
                latest['I', row['target']] = indexes; pending = None
        elif row['kind'] == 'I':
            need(row['text'].isascii() and row['text'].isdigit() and int(row['text']) <= 8192, 'Invalid board guide length')
            need(row['text'] == str(int(row['text'])), 'Noncanonical board guide length')
            pending = row, [index], '', int(row['text'])
        else:
            need(row['kind'] in 'OPD', 'Invalid board policy kind')
            if row['kind'] == 'P':
                need(not row['text'] or re.fullmatch(r'[0-9a-f]{32} [0-9a-f]{64}', row['text']), 'Invalid board password')
            if row['kind'] == 'D':
                need(row['target'] != 1 and row['target'] not in deleted and not row['text'], 'Invalid board deletion')
                deleted.add(row['target'])
            if row['kind'] == 'O':
                pieces = row['text'].split(' ')
                need(len(pieces) == 5 and all(s.isascii() and s.isdigit() and s == str(int(s)) for s in pieces[1:]), 'Invalid board options')
            latest[row['kind'], row['target']] = [index]
    need(pending is None, 'Incomplete board guide')
    if magic == b'LNBDP001': retention.access_limits(source) # Validate option/password/delete fields before pruning.
    keep = {i for indexes in latest.values() for i in indexes}
    return retention.encode(magic, [r for i, r in enumerate(rows) if i in keep])


def tool_path(directory, name):
    p = Path(directory) / (name + ('.exe' if os.name == 'nt' else ''))
    if not p.is_file():
        p = p.with_name('liner-notes-' + p.name)
    need(p.is_file(), f'Missing {p}; run make all, or specify --tools-dir')
    return str(p.resolve())


def command(*args):
    result = subprocess.run(list(map(str, args)), capture_output=True)
    need(result.returncode == 0, 'State compaction failed: ' + result.stderr.decode('utf-8', 'replace')[:500])


def compact_all(root, manifest, tools_dir):
    memberctl, vaultctl = tool_path(tools_dir, 'memberctl'), tool_path(tools_dir, 'vaultctl')
    reports = []
    def publish(path, output, kind):
        before, after = path.read_bytes(), output.read_bytes()
        reports.append(dict(path=path.relative_to(root).as_posix(), kind=kind, input_bytes=len(before), output_bytes=len(after), saved_bytes=len(before)-len(after), source_sha256=hashlib.sha256(before).hexdigest(), output_sha256=hashlib.sha256(after).hexdigest()))
        os.replace(output, path)
    with tempfile.TemporaryDirectory(prefix='.liner-state-', dir=root.parent) as work:
        work = Path(work)
        members = root / manifest['members']
        out = work / 'members'
        command(memberctl, 'compact', members, out)
        command(memberctl, 'compact', out, work / 'members-validated')
        publish(members, out, 'members-snapshot')
        moderation = root / (manifest['board'] + '.moderation')
        mapping = None
        if moderation.exists():
            data, mapping = moderation_copy(moderation, members)
            out = work / 'moderation'; out.write_bytes(data); os.chmod(out, 0o600)
            with out.open('r+b') as f: os.fsync(f.fileno())
            publish(moderation, out, 'moderation-snapshot')
        for name, kind, transform in [(manifest['members']+'.profile', 'profile-snapshot', profile_copy), (manifest['board']+'.extras', 'article-profile-snapshot', lambda p,m: extras_copy(p,m,mapping)), (manifest['board']+'.boardpolicy', 'board-policy-snapshot', policy_copy), (manifest['board']+'.sigops', 'sigop-snapshot', policy_copy)]:
            path = root / name
            if path.exists():
                out = work / kind; out.write_bytes(transform(path, members)); os.chmod(out, 0o600)
                with out.open('r+b') as f: os.fsync(f.fileno())
                publish(path, out, kind)
        for index, name in enumerate(sorted(manifest['files'])):
            if Path(name).name != 'index.dat': continue
            path = root / name
            with path.open('rb') as f:
                if f.read(8) != b'LNVLT001': continue
            out = work / f'vault-{index}'
            command(vaultctl, 'compact-index', path.parent, out)
            # Validate the generated snapshot through the same native reader in
            # an isolated directory; it does not need blobs to replay the index.
            check = work / f'check-{index}'; check.mkdir(mode=0o700)
            (check / 'index.dat').write_bytes(out.read_bytes())
            command(vaultctl, 'compact-index', check, work / f'checked-{index}')
            publish(path, out, 'vault-snapshot')
            data = path.read_bytes(); removed, removed_bytes = [], 0
            for at in range(8, len(data), 640):
                row = data[at:at+640]
                if row[0] not in b'FX': continue
                ident = u32(row, 4)
                for suffix in (('bin', 'part') if row[0] == ord('X') else ('bin',)):
                    blob = path.parent / f'{ident:08}.{suffix}'
                    if blob.exists():
                        need(blob.is_file() and not blob.is_symlink(), 'Unsafe reclaimed blob')
                        removed_bytes += blob.stat().st_size; removed.append(blob.name); blob.unlink()
            reports[-1]['reclaimed_files'] = removed; reports[-1]['reclaimed_bytes'] = removed_bytes
    handled = {r['path'] for r in reports}
    reports.append(dict(kind='preserved-stores', paths=sorted(n for n in manifest['files'] if n not in handled), note='Catalog/move journals, referenced blobs, configuration and logs without an explicit retention request are preserved. Complete prior history remains in the source backup.'))
    return reports
