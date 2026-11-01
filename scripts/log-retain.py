#!/usr/bin/env python3
"""Offline log retention: preserve input, archive it, and prepare a retained copy.

Run only with the BBS stopped. Replace the active log with retained.dat after
checking report.json and starting a test copy. Article IDs and file IDs do not
change; log record/connection numbers are renumbered. No archive is deleted.
"""
import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import time

MAGICS = {b'LNUSER01', b'LNDOWN01', b'LNCHAT01', b'LNACTV01', b'LNBACC01'}
MAX64 = (1 << 64) - 1


def checksum(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def read_rows(data, allowed=MAGICS):
    if data[:8] not in allowed or (len(data) - 8) % 4096:
        raise ValueError('unsupported or truncated log (state journals must not be pruned)')
    result = []
    for seq, offset in enumerate(range(8, len(data), 4096), 1):
        row = data[offset:offset+4096]
        number, actor, target, stamp, length = struct.unpack_from('<IIIQI', row, 4)
        if number != seq or any(row[1:4]) or length > 3000 or any(row[28+length:4092]) or checksum(row[:4092]) != struct.unpack_from('<I', row, 4092)[0]:
            raise ValueError('invalid log record')
        text = row[28:28+length].decode('utf-8')
        if '\0' in text:
            raise ValueError('embedded NUL')
        result.append(dict(kind=chr(row[0]), seq=seq, actor=actor, target=target, time=stamp, text=text))
    return data[:8], result


def numbers(text, count):
    if not re.fullmatch(r'[0-9]+(?: [0-9]+){%d}' % (count-1), text):
        raise ValueError('invalid summary or elapsed time')
    values = list(map(int, text.split()))
    if any(v > MAX64 for v in values):
        raise ValueError('integer overflow')
    return values


def add_stats(totals, user, logins, elapsed, last):
    old = totals.setdefault(user, [0, 0, 0])
    old[:] = [old[0]+logins, old[1]+elapsed, max(old[2], last)]
    if max(old) > MAX64:
        raise ValueError('aggregate overflow')


def user_retention(rows, cutoff):
    base, live, sessions = {}, {}, []
    started = False
    for row in rows:
        kind, user, target = row['kind'], row['actor'], row['target']
        if not 0 <= user <= 999999:
            raise ValueError('bad user')
        if kind == 'S':
            if started or not user or target or user in base:
                raise ValueError('bad user summary')
            values = numbers(row['text'], 3)
            if not values[0] or values[2] > 253402300799:
                raise ValueError('bad summary values')
            base[user] = values
            continue
        started = True
        if kind == 'C':
            if user or target or row['text'] not in ('RAW', 'TELNET') or len(live) >= 64:
                raise ValueError('bad connection')
            current = dict(rows=[row], user=0, elapsed=0, stats={}, ended=False)
            live[row['seq']] = current
            sessions.append(current)
            continue
        current = live.get(target)
        if current is None:
            raise ValueError('missing connection')
        if kind == 'L':
            if not user or current['user'] or row['text']:
                raise ValueError('bad login')
            current['user'], current['elapsed'] = user, 0
            add_stats(current['stats'], user, 1, 0, row['time'])
        elif kind in ('T', 'O', 'E', 'X'):
            elapsed, = numbers(row['text'], 1)
            if user != current['user'] or elapsed < current['elapsed'] or (not user and elapsed):
                raise ValueError('bad duration')
            if user:
                add_stats(current['stats'], user, 0, elapsed-current['elapsed'], 0)
            current['elapsed'] = elapsed
            if kind == 'O':
                current['user'], current['elapsed'] = 0, 0
            elif kind in ('E', 'X'):
                current['ended'] = True
                del live[target]
        else:
            raise ValueError('bad user event')
        current['rows'].append(row)
    keep = set()
    for session in sessions:
        # Retain the whole connection to preserve login/logout dependencies.
        if session['ended'] and all(r['time'] and r['time'] < cutoff for r in session['rows']):
            for user, values in session['stats'].items():
                add_stats(base, user, *values)
        else:
            keep.update(r['seq'] for r in session['rows'])
    summaries = [dict(kind='S', actor=u, target=0, time=0, text=' '.join(map(str, v))) for u, v in sorted(base.items())]
    return summaries + [r for r in rows if r['seq'] in keep]


def download_retention(rows, cutoff):
    base, keep = {}, []
    started = False
    for row in rows:
        target = row['target']
        if row['actor'] or not 1 <= target <= 0x7ffffffe:
            raise ValueError('bad download file')
        if row['kind'] == 'S':
            if started or target in base:
                raise ValueError('bad download summary')
            value, = numbers(row['text'], 1)
            if not value:
                raise ValueError('zero summary')
            base[target] = value
        elif row['kind'] in ('D','d'):
            started = True
            parts = row['text'].split(' ')
            if len(parts) != (4 if row['kind']=='d' else 3) or not parts[0] or len(parts[0].encode()) > 79 or any(c.isspace() for c in parts[0]) or not re.fullmatch('[0-9a-f]{32}', parts[1]):
                raise ValueError('bad receipt')
            if row['kind']=='d' and parts[3] not in ('LINE','ZMODEM','XMODEM','YMODEM','YMODEM-G','MLINK','TEXT'):raise ValueError('bad protocol')
            size, = numbers(parts[2], 1)
            if size > 1024*1024*1024:
                raise ValueError('bad download size')
            if row['time'] and row['time'] < cutoff:
                base[target] = base.get(target, 0)+1
                if base[target] > MAX64:
                    raise ValueError('count overflow')
            else:
                keep.append(row)
        else:
            raise ValueError('bad download event')
    return [dict(kind='S', actor=f, target=0, time=0, text=str(n)) | dict(actor=0, target=f) for f,n in sorted(base.items())] + keep


def access_limits(path):
    with locked(Path(path)) as stream:
        _,rows=read_rows(stream.read(),{b'LNBDP001'})
    limits={i:1000 for i in range(1,129)}
    for row in rows:
        if row['kind']=='O':
            fields=row['text'].split(' ')
            if len(fields)!=5 or fields[0] not in ('BOARD','MENU') or not 1<=row['target']<=1024:
                raise ValueError('bad board options')
            order,articles,size,keep=numbers(' '.join(fields[1:]),4)
            if order>0xffffffff or articles>0x7ffffffe or size>1099511627776 or keep>100000:
                raise ValueError('bad board option limits')
            limits[row['target']]=keep
        elif row['kind']=='D':
            if not 1<=row['target']<=1024:raise ValueError('bad deleted board')
            limits[row['target']]=0
    return limits


def retain(magic, rows, cutoff, gather_count, board_keep=None):
    if magic==b'LNBACC01':
        if board_keep is None:raise ValueError('board policy is required for access-log retention')
        retained=[];counts={}
        for row in reversed(rows):
            board,=numbers(row['text'],1)
            if row['kind'] not in ('R','P') or not 1<=board<=1024 or not 0<=row['actor']<=999999 or not 1<=row['target']<=0x7ffffffe:
                raise ValueError('bad article access event')
            counts[board]=counts.get(board,0)+1
            if counts[board]<=board_keep[board] and (not row['time'] or row['time']>=cutoff):retained.append(row)
        return list(reversed(retained))
    if magic == b'LNUSER01':
        return user_retention(rows, cutoff)
    if magic == b'LNDOWN01':
        return download_retention(rows, cutoff)
    for row in rows:
        if not 1 <= row['actor'] <= 999999:
            raise ValueError('bad actor')
        if magic == b'LNCHAT01':
            pieces = row['text'].split('\t')
            if row['kind'] != 'S' or row['target'] or len(pieces) != 3 or any(not s or any(ord(c)<32 or ord(c)==127 for c in s) for s in pieces) or any(len(s.encode()) > n for s,n in zip(pieces,(32,80,512))):
                raise ValueError('bad chat event')
        elif row['kind'] == 'G':
            lines = row['text'].split('\r\n')
            if row['target'] or lines[-1] or not 1 <= len(lines)-1 <= 10 or any(len(s)>70 or any(ord(c)<32 or ord(c)==127 for c in s) for s in lines[:-1]):
                raise ValueError('bad Gather message')
        elif row['kind'] not in 'AKSC':
            raise ValueError('bad activity event')
    gathers = [r['seq'] for r in rows if r['kind'] == 'G']
    pinned = set(gathers[-gather_count:]) if gather_count else set()
    return [r for r in rows if not r['time'] or r['time'] >= cutoff or r['seq'] in pinned]


def encode(magic, rows):
    data = bytearray(magic)
    connection_map = {}
    for seq, item in enumerate(rows, 1):
        target = item['target']
        if magic == b'LNUSER01':
            if item['kind'] == 'C':
                connection_map[item['seq']] = seq
            elif item['kind'] != 'S':
                target = connection_map[target]
        text = item['text'].encode('utf-8')
        row = bytearray(4096)
        row[0] = ord(item['kind'])
        struct.pack_into('<IIIQI', row, 4, seq, item['actor'], target, item['time'], len(text))
        row[28:28+len(text)] = text
        struct.pack_into('<I', row, 4092, checksum(row[:4092]))
        data.extend(row)
    return bytes(data)


@contextlib.contextmanager
def locked(path):
    if path.is_symlink():
        raise ValueError('symlinks are not accepted')
    fd = os.open(path, os.O_RDWR | getattr(os,'O_NOFOLLOW',0) | getattr(os,'O_BINARY',0))
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):
            raise ValueError('not a regular file')
        if os.name == 'nt':
            import msvcrt
            msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.lockf(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        with os.fdopen(fd, 'rb', closefd=False) as stream:
            yield stream
    finally:
        os.close(fd)


def saved(path, data):
    fd = os.open(path, os.O_WRONLY|os.O_CREAT|os.O_EXCL|getattr(os,'O_BINARY',0), 0o600)
    with os.fdopen(fd,'wb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def run(source, destination, keep_days, gather_count=16, now=None, check=False, policy_path=None):
    if keep_days < 0 or keep_days > 36500 or not 0 <= gather_count <= 1000:
        raise ValueError('keep-days must be 0..36500; gather-count must be 0..1000')
    cutoff = max(0, int(time.time() if now is None else now) - keep_days*86400) if keep_days else 0
    with locked(Path(source)) as stream:
        original = stream.read()
        magic, rows = read_rows(original)
        board_keep=None
        if magic==b'LNBACC01':
            if policy_path is None:
                if not str(source).endswith('.boardaccess'):raise ValueError('--board-policy is required for renamed access logs')
                policy_path=str(source)[:-len('.boardaccess')]+'.boardpolicy'
            board_keep=access_limits(policy_path)
        selected = retain(magic, rows, cutoff, gather_count, board_keep)
        result = encode(magic, selected)
        # Validate the generated graph/summary as well as binary checksums.
        check_magic, check_rows = read_rows(result)
        retain(check_magic, check_rows, 0, gather_count, board_keep)
        report = dict(format=magic.decode(), keep_days=keep_days, cutoff=cutoff, input_records=len(rows), output_records=len(selected), input_bytes=len(original), output_bytes=len(result), input_sha256=hashlib.sha256(original).hexdigest(), output_sha256=hashlib.sha256(result).hexdigest(), note='Input preserved. Whole USERLOG connections retained; cumulative totals preserved. Log sequence numbers change. Old download receipts are no longer acknowledged, never recounted.')
        if check:
            return report
        output = Path(destination)
        output.mkdir(mode=0o700, parents=False, exist_ok=False)
        # The archive is complete and synced before publishing the retained log.
        saved(output/'archive.dat', original)
        saved(output/'retained.dat', result)
        saved(output/'report.json', (json.dumps(report,ensure_ascii=False,indent=2)+'\n').encode())
        if os.name != 'nt':
            for directory in (output,output.parent):
                fd=os.open(directory,os.O_RDONLY)
                try:os.fsync(fd)
                finally:os.close(fd)
        return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source')
    parser.add_argument('output_directory', nargs='?')
    parser.add_argument('--keep-days',type=int,required=True,help='0 retains all details')
    parser.add_argument('--gather-count',type=int,default=16,help='protect latest Gather messages even when older than retention period')
    parser.add_argument('--board-policy',help='board options journal for renamed article access logs')
    parser.add_argument('--check',action='store_true',help='validate and report without writing')
    args=parser.parse_args()
    if not args.check and not args.output_directory:parser.error('output_directory required unless --check')
    try:print(json.dumps(run(args.source,args.output_directory,args.keep_days,args.gather_count,check=args.check,policy_path=args.board_policy),ensure_ascii=False,indent=2))
    except (OSError,ValueError,KeyError,OverflowError) as exc:parser.exit(1,f'log retention refused: {exc}\n')
