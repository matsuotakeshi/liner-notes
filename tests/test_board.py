"""Stage 2 real-socket and on-disk acceptance tests; no third-party modules."""
import concurrent.futures
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time
import os
import signal
from test_runtime import Client


class Server:
    def __init__(self, binary, store, preexec_fn=None, members=None, catalog=None, menu=False, language=None, telnet=False, admins=None, menu_dir=None, bind=None, vault=None, zhelper=None, options=None):
        args=[binary]+(options or [])+(['--zmodem-helper',str(zhelper)] if zhelper else [])+(['--vault-dir',str(vault)] if vault is not None else [])+(['--bind',bind] if bind is not None else [])+(['--menu-dir',str(menu_dir)] if menu_dir is not None else [])+(['--admins',str(admins)] if admins else [])+(['--telnet'] if telnet else [])+(['--menu'] if menu else [])+(['--lang',language] if language else [])+['0', str(store)]+([str(members)] if members else [])+([str(catalog)] if catalog else [])
        self.proc = subprocess.Popen(args, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True, preexec_fn=preexec_fn)
        result = []
        t = threading.Thread(target=lambda: result.append(self.proc.stdout.readline()), daemon=True)
        t.start(); t.join(float(os.environ.get("LINER_TEST_START_TIMEOUT","5")))
        if not result or not result[0].startswith('LISTEN '):
            self.proc.kill(); self.proc.wait()
            raise AssertionError(('startup', result, self.proc.stderr.read()))
        self.port = int(result[0].split()[-1])
        self.expected_errors = []

    def close(self, crash=False):
        if crash:
            self.proc.kill()
        else:
            self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill(); self.proc.wait(); raise
        errors = self.proc.stderr.read()
        assert errors.splitlines() == self.expected_errors, errors


def post(c, title, body, parent=None):
    cmd = f'REPLY {parent} {title}' if parent else f'POST {title}'
    c.send((cmd+'\r\n').encode()); c.expect(b'EDIT end with . or /CANCEL')
    for line in body.split('\n'):
        if line.startswith('.'):
            line = '.' + line
        c.send((line+'\r\n').encode())
    c.send(b'.\r\n')
    answer = c.line()
    assert answer.startswith(b'SAVED '), answer
    return int(answer.split()[1])


def read(c, number):
    c.send(f'READ {number}\n'.encode())
    header = c.line().split()
    assert header[:2] == [b'ARTICLE', str(number).encode()], header
    parent = int(header[3])
    title = c.line(); assert title.startswith(b'TITLE ')
    size = c.line(); assert size.startswith(b'BODY ')
    n = int(size.split()[1])
    while len(c.buf) < n:
        data = c.sock.recv(8192); assert data
        c.buf += data
    body, c.buf = c.buf[:n], c.buf[n:]
    c.expect(b'END')
    return parent, title[6:].decode(), body.decode()


def reject_store(binary, path, check_bytes=True):
    before = path.read_bytes() if check_bytes else None
    p = subprocess.run([binary, '0', str(path)], capture_output=True, timeout=5)
    assert p.returncode != 0 and (b'board:' in p.stderr or (not check_bytes and b'catalog:' in p.stderr)), (p.returncode, p.stderr)
    if check_bytes:
        assert path.read_bytes() == before, 'rejected store was modified'


def run(binary):
    binary = str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='rtbbs-stage2-') as temp:
        root = pathlib.Path(temp); store = root/'board.dat'
        s = Server(binary, store)
        a = b = None
        try:
            a, b = Client(s.port), Client(s.port)
            a.send(b'POST unfinished\n'); a.expect(b'EDIT end with . or /CANCEL')
            a.send(b'partial draft\n')
            b.send(b'PING\n'); b.expect(b'PONG')
            a.send(b'/CANCEL\n'); a.expect(b'CANCELLED')
            b.send(b'LIST\n'); b.expect(b'LIST 0'); b.expect(b'END NEXT 0')
            first = post(a, '最初の投稿', 'こんにちは\nEND\n.\n2行目')
            assert first == 1
            assert read(b, 1) == (0, '最初の投稿', 'こんにちは\r\nEND\r\n.\r\n2行目\r\n')
            r1 = post(b, '返信その1', '応答1', 1)
            r2 = post(a, '返信その2', '応答2', 1)
            assert read(a, r1)[0] == read(b, r2)[0] == 1
            print('PASS: draft isolation/cancel, UTF-8 bytes, multiline read, two replies')
            b.send(b'REPLY 999 missing\n'); b.expect(b'ERR no parent')
            b.send(b'READ 999\n'); b.expect(b'ERR no article')
            b.send(b'LIST 999999999999999999999\n'); b.expect(b'ERR article number')
            b.send(b'POST '+b'x'*81+b'\n'); b.expect(b'ERR title 1..80 bytes, no controls')
            b.send(b'POST empty\n'); b.expect(b'EDIT end with . or /CANCEL')
            b.send(b'.\n'); b.expect(b'ERR empty body')
            b.send(b'/CANCEL\n'); b.expect(b'CANCELLED')
            print('PASS: invalid parent/id/title and empty body do not create articles')
            def concurrent_post(i):
                c = Client(s.port)
                try:
                    return post(c, f'parallel-{i}', f'body-{i}')
                finally:
                    c.close()
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
                ids = list(pool.map(concurrent_post, range(16)))
            assert sorted(ids) == list(range(4, 20)), ids
            b.send(b'LIST\n'); b.expect(b'LIST 19')
            for i in range(1, 6):
                assert b.line().startswith(f'{i} '.encode())
            b.expect(b'END NEXT 6')
            print('PASS: concurrent posts receive unique contiguous IDs; list pagination')
            reject_store(binary, store, check_bytes=False)
            print('PASS: second process cannot open the same store')
            a.close(); b.close(); a = b = None
        finally:
            if a: a.close()
            if b: b.close()
            s.close()
        s = Server(binary, store)
        c = Client(s.port)
        try:
            assert read(c, 1)[1] == '最初の投稿'
            assert read(c, 2)[0] == read(c, 3)[0] == 1
            assert post(c, 'after restart', 'persist') == 20
        finally:
            # Crash after the acknowledged post while the guest is idle.
            # Closing the socket first races SIGKILL against the userlog's
            # disconnect write, testing a truncated log instead of article durability.
            try:
                s.close(crash=True)
            finally:
                c.close()
        s = Server(binary, store); c = Client(s.port)
        try:
            assert read(c, 20) == (0, 'after restart', 'persist\r\n')
            c.send(b'POST disconnected draft\n'); c.expect(b'EDIT end with . or /CANCEL')
            c.send(b'not committed\n'); c.close()
            time.sleep(0.1)
            c = Client(s.port)
            c.send(b'LIST 20\n'); c.expect(b'LIST 20')
            assert c.line() == b'20 0 after restart'; c.expect(b'END NEXT 0')
        finally:
            c.close(); s.close()
        print('PASS: clean restart and forced process termination retain acknowledged posts')
        print('PASS: disconnected draft is not saved')
        original = store.read_bytes()
        corrupt = root/'corrupt.dat'; raw = bytearray(original); raw[-12] ^= 1
        corrupt.write_bytes(raw); reject_store(binary, corrupt)
        partial = root/'partial.dat'; partial.write_bytes(original[:-10]); reject_store(binary, partial)
        version = root/'version.dat'; version.write_bytes(b'RTBOARD9'+original[8:]); reject_store(binary, version)
        print('PASS: corrupt/truncated/unknown-version files rejected without modification')
        if os.name == 'posix':
            import fault_io as resource
            def limited_file_size():
                signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
                resource.setrlimit(resource.RLIMIT_FSIZE, (128, 128))
            broken = root/'limited.dat'
            s = Server(binary, broken, preexec_fn=limited_file_size,options=["--user-log","-"])
            c = Client(s.port); peer = Client(s.port)
            try:
                c.send(b'POST failure\n'); c.expect(b'EDIT end with . or /CANCEL')
                c.send(b'cannot fit\n.\n')
                c.expect(b'ERR storage uncertain; restart inspection required')
                peer.send(b'PING\n'); peer.expect(b'PONG')
                peer.send(b'LIST\n'); peer.expect(b'LIST 0'); peer.expect(b'END NEXT 0')
                c.send(b'POST again\n'); c.expect(b'EDIT end with . or /CANCEL')
                c.send(b'no more writes\n.\n'); c.expect(b'ERR storage unavailable')
            finally:
                c.close(); peer.close(); s.close()
            assert broken.stat().st_size == 128
            reject_store(binary, broken)
            print('PASS: real OS short-write failure is not acknowledged/published; peers survive')


if __name__ == '__main__':
    run(sys.argv[1])
