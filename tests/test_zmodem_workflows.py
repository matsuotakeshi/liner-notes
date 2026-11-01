"""Protocol-independent publication/recovery policies, no ZMODEM installation."""
import hashlib
import json
import pathlib
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import vault_zmodem as gateway
from zmodem_connector import safe_name, staged_file, TransferFailure


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


if __name__ == '__main__': run()
