#!/usr/bin/env python3
"""Offline, copy-only board/mail compaction. Never replaces the input file."""
import argparse
import contextlib
import hashlib
import json
import os
import pathlib
import stat
import struct
import tempfile

MAX_BODY = 262144

def need(value, message):
    if not value:
        raise ValueError(message)

def u32(data, pos):
    return struct.unpack_from('<I', data, pos)[0]

def put(data, pos, value):
    struct.pack_into('<I', data, pos, value)

def checksum(data):
    result = 2166136261
    for value in data:
        result = ((result ^ value) * 16777619) & 0xffffffff
    return result

def sealed(data):
    return bytes(data) + struct.pack('<I', checksum(data))

def exact(stream, size):
    data = stream.read(size)
    need(len(data) == size, 'Truncated record')
    return data

def valid_checksum(row):
    need(checksum(row[:-4]) == u32(row, len(row)-4), 'Checksum mismatch')

def board_records(stream, stats, moves=(), withdrawn=frozenset()):
    magic = exact(stream, 8)
    need(magic in (b'RTBOARD1', b'RTBOARD2', b'RTBOARD3', b'RTBOARD4', b'RTBOARD5'), 'Unknown board format')
    version = min(4,magic[-1] - 48)
    yield b'RTBOARD5' if magic==b'RTBOARD5' else b'RTBOARD4'
    boards = [0]
    roots = [0]
    parents = [0]
    moved = 0
    while True:
        prefix = stream.read(16)
        if not prefix:
            break
        need(len(prefix) == 16, 'Truncated board record')
        encoded, parent, tl, bl = struct.unpack('<4I', prefix)
        compact, ident = bool(encoded & 0x80000000), encoded & 0x7fffffff
        need((not compact or version == 4) and 0 < bl <= (MAX_BODY if compact else 1024), 'Invalid body size')
        need(ident == len(boards) and ident <= 0x7ffffffe and parent < ident and 0 < tl <= 80, 'Invalid article identity/title')
        base = 100 + bl if compact else 1124
        size = base + (8 if version >= 2 else 0) + (32 if version >= 3 else 0) + (4 if version == 4 else 0)
        row = prefix + exact(stream, size-16)
        valid_checksum(row)
        title, body = row[16:16+tl], row[96:96+bl]
        need(b'\0' not in title and b'\0' not in body, 'Embedded NUL')
        timestamp = row[base-4:base+4] if version >= 2 else b'\0'*8
        need(struct.unpack('<Q', timestamp)[0] <= 253402300799, 'Invalid date')
        author = row[base+4:base+36] if version >= 3 else b'\0'*32
        board = u32(row, base+36) if version == 4 else 1
        need(0 < board <= 10000 and (not parent or boards[parent] == board), 'Invalid board/reply relationship')
        boards.append(board)
        roots.append(roots[parent] if parent else ident)
        parents.append(parent)
        while moved<len(moves) and moves[moved][3]<=ident:
            root,origin,destination,through,kind=moves[moved]
            need(through==ident and (kind=='S' or roots[root]==root) and boards[root]==origin,'Invalid move checkpoint')
            def belongs(i):
                while i>=root:
                    if i==root:return True
                    i=parents[i]
                return False
            for i in range(root,through+1):
                if belongs(i) if kind=='S' else roots[i]==root:
                    need(boards[i]==origin,'Mixed-board moved thread');boards[i]=destination
            if kind=='S':
                parents[root]=0
                for i in range(1,through+1):roots[i]=roots[parents[i]] if parents[i] else i
            moved+=1
        if ident in withdrawn:
            stats['reclaimed_articles'] = stats.get('reclaimed_articles', 0) + 1
            stats['reclaimed_body_bytes'] = stats.get('reclaimed_body_bytes', 0) + max(0, len(body)-2)
            body, bl = b'\r\n', 2
        out = struct.pack('<4I', ident | 0x80000000, parent, tl, bl) + title.ljust(80, b'\0') + body + timestamp + author + struct.pack('<I', board)
        yield sealed(out)
    need(moved==len(moves),'Move references missing articles')
    stats['articles'] = len(boards)-1

def board_moves(path):
    if not path.exists():return []
    rows=[]
    with locked(path) as f:
        need(exact(f,8)==b'LNBMOV01','Unknown board move format')
        sequence=0
        while raw:=f.read(4096):
            need(len(raw)==4096,'Truncated move journal');valid_checksum(raw);sequence+=1
            size=u32(raw,24);root=u32(raw,12)
            need(raw[0] in (ord('M'),ord('S')) and not any(raw[1:4]) and u32(raw,4)==sequence and 0<u32(raw,8)<=999999 and size<=80 and not any(raw[28+size:-4]),'Invalid move record')
            text=raw[28:28+size].decode('ascii');parts=text.split(' ')
            need(len(parts)==3 and all(p.isdecimal() for p in parts),'Invalid move fields')
            origin,destination,through=map(int,parts)
            need(text==f'{origin} {destination} {through}' and 0<origin<=1024 and 0<destination<=1024 and origin!=destination and 0<root<=through<=0x7ffffffe and (not rows or through>=rows[-1][3]),'Invalid move bounds')
            rows.append((root,origin,destination,through,chr(raw[0])))
    return rows

def text_ok(text, body=False, empty=False):
    if not text:
        return empty
    if body:
        if not text.endswith(b'\r\n'):
            return False
        text = text.replace(b'\r\n', b'')
    return all(value >= 32 and value != 127 for value in text)

def mail_records(stream, stats):
    need(exact(stream, 8) == b'LNMAIL01', 'Unknown mail format')
    messages, drafts, sequence = [], {}, 0
    used_attachments = set()
    while True:
        prefix = stream.read(128)
        if not prefix:
            break
        need(len(prefix) == 128, 'Truncated mail record')
        multiple = prefix[0] in b'ux'
        attach_count = prefix[2] if multiple else int(prefix[0] in b'tv')
        need(attach_count <= 16 and (not multiple or attach_count >= 2), 'Invalid attachment count')
        kind = 'S' if prefix[0] in b'tu' else 'W' if prefix[0] in b'vx' else chr(prefix[0]).upper()
        need(kind in ('S','G','W','R','D','C','A'), 'Unknown mail record')
        bl = u32(prefix, 32)
        compact = prefix[0] in b'swrdctvagux'
        need(bl <= (MAX_BODY if compact else 1024), 'Invalid mail body size')
        size = 128 + bl + (4*attach_count) if compact else 1152
        row = prefix + exact(stream, size-128)
        valid_checksum(row)
        ident, actor = u32(row, 8), u32(row, 12)
        sequence += 1
        need(sequence <= 0xffffffff and u32(row,4) == sequence, 'Invalid mail sequence')
        need(((kind=='G' and actor==0) or 0 < actor <= 999999) and row[3] == 0 and (multiple or row[2] == 0), 'Invalid actor/reserved fields')
        need(not row[1] or (kind == 'S' and row[1] == 1 and actor in drafts), 'Invalid draft consumption')
        if kind in ('S','G','W'):
            sender, recipient, parent, tl = (u32(row, p) for p in (16,20,24,28))
            need(kind != 'G' or (sender==0 and parent==0 and not row[1]), 'Invalid guest mail')
            need(sender == actor and 0 < recipient <= 999999 and parent <= len(messages), 'Invalid mail participants/parent')
            need(0 < tl <= 80 and struct.unpack_from('<Q', row, 36)[0] <= 253402300799, 'Invalid mail title/date')
            if parent:
                old = messages[parent-1]
                need(not old['deleted'] and u32(old['row'],20) == sender and u32(old['row'],16) == recipient, 'Invalid reply relationship')
            attachment = [u32(row,124+bl+4*i) for i in range(attach_count)]
            need(len(set(attachment)) == len(attachment) and all(0 < n <= 0x7ffffffe for n in attachment), 'Invalid attachment')
            used_attachments.update(attachment)
            need(not any(row[44+tl:124]) and not any(row[124+bl+(4*attach_count):-4]), 'Nonzero mail padding')
            need(text_ok(row[44:44+tl]) and text_ok(row[124:124+bl], True, kind=='W'), 'Invalid mail text')
            item = {'row': row[:124+bl], 'read': False, 'deleted': False, 'attachment': attachment}
            if kind == 'W':
                need(ident == sequence, 'Invalid draft revision')
                drafts[actor] = item
            else:
                need(ident == len(messages)+1 and ident <= 0x7ffffffe, 'Invalid mail ID')
                messages.append(item)
                if row[1]:
                    del drafts[actor]
        else:
            need(not any(row[16:-4]), 'Invalid control record')
            if kind == 'A':
                need(0 < ident <= 0x7ffffffe, 'Invalid attachment marker'); used_attachments.add(ident)
            elif kind == 'C':
                need(actor in drafts and u32(drafts[actor]['row'],8) == ident, 'Invalid draft deletion')
                del drafts[actor]
            else:
                need(0 < ident <= len(messages), 'Unknown mail ID')
                item = messages[ident-1]
                need(not item['deleted'] and actor in (u32(item['row'],16),u32(item['row'],20)), 'Invalid mail access')
                if kind == 'R':
                    need(actor == u32(item['row'],20), 'Invalid read actor')
                    item['read'] = True
                else:
                    item['deleted'] = True
    yield b'LNMAIL01'
    new_sequence = 0
    def emit(item, kind, ident=None, attachment=()):
        nonlocal new_sequence
        new_sequence += 1
        row = bytearray(item)
        row[0] = ord((('t' if kind=='s' else 'v') if len(attachment)==1 else ('u' if kind=='s' else 'x')) if attachment else 'g' if kind=='s' and u32(row,16)==0 else kind); row[1] = 0
        row[2] = len(attachment) if len(attachment)>1 else 0
        for ident_att in attachment:row += struct.pack('<I',ident_att)
        put(row,4,new_sequence)
        if ident is not None:
            put(row,8,ident if ident else new_sequence)
        return sealed(row)
    for item in messages:
        row = item['row']
        if item['deleted']:
            # Keep IDs/relationships while removing deleted private text.
            row = bytearray(row[:124]); row[44:124] = b'\0'*80
            row[44:53] = b'[deleted]'; put(row,28,9); put(row,32,2)
            row += b'\r\n'
        yield emit(row, 's', attachment=item['attachment'])
    # Drafts may refer to a subsequently deleted message. Rebuild before D.
    for user in sorted(drafts):
        yield emit(drafts[user]['row'], 'w', 0, drafts[user]['attachment'])
    for ident, item in enumerate(messages, 1):
        for kind, enabled, actor in [('r',item['read'],u32(item['row'],20)), ('d',item['deleted'],u32(item['row'],16) or u32(item['row'],20))]:
            if enabled:
                row = bytearray(124); put(row,8,ident); put(row,12,actor)
                yield emit(row, kind)
    for attachment in sorted(used_attachments):
        row=bytearray(124);put(row,8,attachment);put(row,12,1)
        yield emit(row,'a')
    stats.update(messages=len(messages), drafts=len(drafts), deleted_bodies_removed=sum(m['deleted'] for m in messages), old_records=sequence, new_records=new_sequence)

@contextlib.contextmanager
def locked(path):
    need(not path.is_symlink(), 'Input must not be a symlink')
    fd = os.open(path, os.O_RDWR | getattr(os,'O_NOFOLLOW',0) | getattr(os,'O_BINARY',0))
    try:
        need(stat.S_ISREG(os.fstat(fd).st_mode), 'Input must be a regular file')
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

def withdrawn_articles(source):
    path = pathlib.Path(str(source)+'.extras')
    need(path.is_file(), 'Withdrawal journal is required for reclamation')
    removed = set()
    with locked(path) as stream:
        need(exact(stream,8)==b'RTEXTRA1','Invalid withdrawal journal')
        seq=0
        while row:=stream.read(512):
            need(len(row)==512,'Truncated withdrawal journal');valid_checksum(row);seq+=1
            n=u32(row,16);kind=chr(row[0])
            need(u32(row,4)==seq and 0<u32(row,12)<=999999 and kind in 'DATHBROIWK' and n<=320 and row[1]<=1 and not any(row[2:4]),'Invalid withdrawal record')
            need(not any(row[i] for i in range(20+n,508) if not row[1] or i<340 or i>=352),'Invalid withdrawal padding')
            need(not any(c<32 or c==127 for c in row[20:20+n]),'Invalid withdrawal text')
            if kind=='D':
                need(n==0 and 0<u32(row,8)<=0x7ffffffe,'Invalid withdrawn article');removed.add(u32(row,8))
    return removed


def compact(kind, source, output=None, reclaim_withdrawn=False):
    source = pathlib.Path(source)
    destination = pathlib.Path(output).absolute() if output else None
    if destination:
        need(not destination.exists() and not destination.is_symlink(), 'Output must not exist')
        need(destination.parent.is_dir(), 'Output directory must exist')
    temporary = None
    try:
        with locked(source) as stream:
            stats = {'kind':kind, 'source_bytes':os.fstat(stream.fileno()).st_size}
            digest = hashlib.sha256(); size = 0
            with contextlib.ExitStack() as stack:
                out = None
                if destination:
                    fd, temporary = tempfile.mkstemp(prefix='.liner-compact-', dir=destination.parent)
                    out = stack.enter_context(os.fdopen(fd,'wb'))
                moves=board_moves(pathlib.Path(str(source)+'.boardmoves')) if kind=='board' else []
                if moves:stats['required_sidecar']=str(source)+'.boardmoves'
                for chunk in (board_records(stream,stats,moves,withdrawn_articles(source) if reclaim_withdrawn else frozenset()) if kind=='board' else mail_records(stream,stats)):
                    digest.update(chunk); size += len(chunk)
                    if out:
                        out.write(chunk)
                if out:
                    out.flush(); os.fsync(out.fileno())
            stream.seek(0); original = hashlib.file_digest(stream, 'sha256').hexdigest()
            if destination:
                # Atomic publication without overwriting any existing path.
                os.link(temporary, destination)
                if os.name != 'nt':
                    directory = os.open(destination.parent, os.O_RDONLY)
                    try: os.fsync(directory)
                    finally: os.close(directory)
            stats.update(output_bytes=size, source_sha256=original, output_sha256=digest.hexdigest(), saved_bytes=stats['source_bytes']-size, check_only=not bool(destination))
            return stats
    finally:
        if temporary:
            os.unlink(temporary)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=['board','mail'])
    parser.add_argument('input')
    parser.add_argument('output', nargs='?')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--reclaim-withdrawn', action='store_true')
    args = parser.parse_args()
    if bool(args.output) == args.check:
        parser.error('Specify either a new output path or --check')
    try:
        print(json.dumps(compact(args.kind,args.input,args.output,args.reclaim_withdrawn),ensure_ascii=True,sort_keys=True))
    except (OSError,ValueError) as error:
        parser.exit(1, 'Compaction failed; source unchanged: '+str(error)+'\n')

if __name__ == '__main__':
    main()
