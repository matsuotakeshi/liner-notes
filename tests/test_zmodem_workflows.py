"""Protocol-independent publication/recovery policies, no ZMODEM installation."""
import hashlib
import json
import pathlib
import sys
import time
import subprocess
import tempfile
import os
import stat

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import vault_zmodem as gateway
import posix_locks
from zmodem_connector import safe_name, staged_file, TransferFailure
from zmodem_host import normalize_personal_text
from transfer_spool import manage, personal_stage


def spool_contents(root):
    """Only Solaris' one validated infrastructure file is not payload residue."""
    entries = list(root.iterdir())
    marker = root / posix_locks.NAMESPACE_FILE
    if marker in entries:
        assert posix_locks.RECORD_LOCKS, 'Unexpected namespace metadata on native flock'
        info = marker.lstat()
        assert stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and info.st_size == 0
        assert info.st_mode & 0o077 == 0 and info.st_uid == os.geteuid()
        entries.remove(marker)
    return entries


class Member:
    chunk_size = 257
    def __init__(self):
        self.protocol = self
        self.data = bytearray()
        self.started = False
        self.suspended = False
        self.lost_commit = False
    def request(self, command, predicate):
        if self.suspended: raise ValueError('revoked')
        assert command == 'ADMIN'
        return b'ROLE member'
    def begin(self, size, digest, name, description):
        self.request('ADMIN', None)
        self.started = True
        self.expected = size, digest
        return 1
    def status(self, ident): return 'ACTIVE', len(self.data), *self.expected
    def write(self, ident, offset, data):
        assert offset == len(self.data)
        self.data.extend(data)
        return len(self.data)
    def finish(self, ident):
        assert self.info(ident) == self.expected
        if self.lost_commit: raise OSError('lost receipt')
        return self.expected
    def info(self, ident): return len(self.data), hashlib.sha256(self.data).hexdigest()
    def read(self, ident, offset): return bytes(self.data[offset:offset+768])


class Engine:
    def __init__(self, payload): self.payload = payload
    def receive(self, directory, input_fd, output_fd, authorize):
        authorize()
        path = directory / 'sample.bin'
        path.write_bytes(self.payload)
        return path
    def send(self, path, input_fd, output_fd, authorize):
        authorize()
        assert path.read_bytes() == self.payload


def fails(call, kind=ValueError):
    try: call()
    except kind: pass
    else: raise AssertionError('Unexpected success')


def run():
    payload = bytes(range(256)) * 10 + b'\r\n\x1b\x00\xff'
    with tempfile.TemporaryDirectory() as d:
        p = pathlib.Path(d)
        member = Member()
        result = gateway.receive(member, Engine(payload), 'localhost', 2222, 'alice', p)
        assert result == (1, len(payload), hashlib.sha256(payload).hexdigest())
        assert not list(p.iterdir())
        assert gateway.send(member, Engine(payload), 1, p) == result[1:]
        assert not list(p.iterdir())
        member = Member(); member.suspended = True
        fails(lambda: gateway.receive(member, Engine(payload), 'localhost', 1, 'alice', p))
        assert not member.started and not list(p.iterdir())
        class Cancelled(Engine):
            def receive(self, directory, *args):
                (directory/'partial').write_bytes(b'partial')
                raise TransferFailure('cancelled')
        member = Member()
        fails(lambda: gateway.receive(member, Cancelled(payload), 'localhost', 1, 'alice', p))
        assert not member.started and not list(p.iterdir())
        member.lost_commit = True
        try: gateway.receive(member, Engine(payload), 'localhost', 1, 'alice', p)
        except gateway.PublicationFailure as error:
            state = json.loads((error.directory/'upload.json').read_text())
            assert state['id'] == 1 and state['phase'] == 'active'
            assert state['connector'] == 'line-v1' and 'password' not in state
            assert pathlib.Path(state['source']).read_bytes() == payload
        else: raise AssertionError('Lost commit was acknowledged')
        for name in ('../x', '/tmp/x', '.hidden', 'a\\b', 'a:b', 'x\x1b[2J', '\udcff'):
            fails(lambda: safe_name(name))
        isolated = p/'inspection'; isolated.mkdir()
        (isolated/'one').write_bytes(b'')
        (isolated/'two').write_bytes(b'')
        fails(lambda: staged_file(isolated))
        assert gateway.MESSAGES['en'].keys() == gateway.MESSAGES['ja'].keys()
    print('PASS: replaceable ZMODEM engine, hash verification, no publication after cancellation/revocation, lost commit recovery and bilingual messages')


def test_personal_content():
    exact = (b'a'*78+b'\r\n')*128
    assert normalize_personal_text(exact) == exact
    assert normalize_personal_text(b'') == b''
    assert normalize_personal_text(b'\xef\xbb\xbf'+ '一行\n二行\r三行'.encode()) == '一行\r\n二行\r\n三行\r\n'.encode()
    for data in (exact+b'x', b'\n'*5121, b'\x00', b'\xc0\xaf', b'\x1b[2J', b'\xc2\x9b2J'):
        try:
            normalize_personal_text(data)
        except ValueError:
            continue
        raise AssertionError('Received invalid personal text')


def test_personal_spool():
    with tempfile.TemporaryDirectory(prefix='personal-spool-') as temporary:
        root = pathlib.Path(temporary)
        with personal_stage(root, 7, 12) as (data, _):
            (data/'in.txt').write_text('temporary', encoding='utf-8')
            row, = manage(root, 'list')
            assert row['phase'] == 'active'
            try:
                manage(root, 'remove', data.parent.name, True)
            except BlockingIOError:
                pass
            else:
                raise AssertionError('Removed another active transfer')
        assert not spool_contents(root)
        # Stop a helper without executing its finally block. The external
        # receiver inherits the lease, so the staged file stays active.
        proc = subprocess.Popen([sys.executable, '-c', '''
from pathlib import Path
import os, subprocess, sys
from transfer_spool import personal_stage
with personal_stage(sys.argv[1], 8, 13) as (data, lease):
    program = 'for i in 1 2 3 4 5 6 7 8 9 10; do [ ! -f "$1" ] || exit; sleep 1; done'
    child = subprocess.Popen(['/bin/sh', '-c', program, 'sh', sys.argv[1]+'/stop'],
                             pass_fds=(lease,), stdout=subprocess.DEVNULL)
    (data/'in.txt').write_text('interrupted')
    print(child.pid, flush=True)
    os._exit(0)
''', str(root)], env={**os.environ, 'PYTHONPATH': str(pathlib.Path(gateway.__file__).parent)},
                                stdout=subprocess.PIPE, text=True)
        with proc.stdout:
            int(proc.stdout.readline())
        assert proc.wait(timeout=5) == 0
        row, = manage(root, 'list')
        assert row['phase']=='active'
        (root/'stop').touch()
        deadline = time.monotonic()+5
        while time.monotonic()<deadline:
            row, = manage(root, 'list')
            if row['phase']=='interrupted':
                break
            time.sleep(.02)
        assert row['phase']=='interrupted'
        assert not manage(root, 'remove', row['id'])['applied']
        assert manage(root, 'remove', row['id'], True)['applied']
        (root/'stop').unlink()
        assert not spool_contents(root)


if __name__ == '__main__':
    test_personal_content()
    test_personal_spool()
    run()
