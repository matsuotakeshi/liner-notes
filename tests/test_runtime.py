"""Black-box socket checks. Runs the same probe on Windows and FreeBSD."""
import pathlib
import socket
import subprocess
import sys
import time
import threading


class Client:
    def __init__(self, port):
        self.sock = socket.create_connection(('127.0.0.1', port), timeout=2)
        self.buf = b''
        self.greeting = self.line()
        assert self.greeting.startswith(b'RTBBS-PROBE '), self.greeting

    def send(self, data):
        self.sock.sendall(data)

    def line(self):
        while b'\n' not in self.buf:
            data = self.sock.recv(8192)
            if not data:
                raise AssertionError('unexpected disconnect')
            self.buf += data
        line, self.buf = self.buf.split(b'\n', 1)
        return line.rstrip(b'\r')

    def expect(self, data):
        actual = self.line()
        assert actual == data, (actual, data)

    def close(self):
        self.sock.close()


def run(binary):
    process = subprocess.Popen([str(pathlib.Path(binary).resolve()), '0'],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    clients = []
    try:
        # Bound startup even if the executable never announces its port.
        startup = []
        reader = threading.Thread(target=lambda: startup.append(process.stdout.readline()), daemon=True)
        reader.start(); reader.join(5)
        assert startup and startup[0].startswith('LISTEN 127.0.0.1 '), startup
        port = int(startup[0].split()[-1])
        def connect():
            c = Client(port); clients.append(c); return c
        a, b = connect(), connect()
        assert a.greeting != b.greeting
        a.send(b'ECHO unfinished')
        b.send(b'PING\r\nWHO\r\n'); b.expect(b'PONG'); b.expect(b'WHO 2')
        a.send(b'\r\n'); a.expect(b'unfinished')
        print('PASS: partial input does not block another session')
        start = time.monotonic()
        a.send(b'WAIT 1200\r\nECHO after-wait\r\n'); a.expect(b'WAITING')
        b.send(b'PING\n'); b.expect(b'PONG')
        assert time.monotonic()-start < 0.8, 'other session blocked by timer'
        a.expect(b'AWAKE'); assert time.monotonic()-start >= 1.1
        a.expect(b'after-wait')
        print('PASS: timer yields; queued commands resume in order')
        for i in range(50):
            b.send(f'ECHO line-{i}\n'.encode()); b.expect(f'line-{i}'.encode())
        print('PASS: sequential command data remains intact')
        a.send(b'WAIT 1200\n'); a.expect(b'WAITING'); a.close()
        c = connect(); c.send(b'ECHO fresh\n'); c.expect(b'fresh')
        time.sleep(1.3)
        c.send(b'PING\n'); c.expect(b'PONG')
        print('PASS: disconnect/reconnect does not inherit old timer or input')
        bad = connect(); bad.send(b'x'*4097)
        try:
            assert bad.sock.recv(1) == b'', 'overlong unfinished input not disconnected'
        except ConnectionResetError:
            pass  # Early rejection can leave unread TCP bytes, resulting in RST.
        b.send(b'PING\n'); b.expect(b'PONG')
        print('PASS: bounded input failure is isolated')
        slow = connect()
        failures = []
        def flood():
            try:
                for _ in range(10000):
                    slow.send(b'ECHO ' + b'x'*900 + b'\n')
            except (OSError, TimeoutError):
                pass
            except Exception as exc:
                failures.append(str(exc))
        writer = threading.Thread(target=flood, daemon=True); writer.start()
        time.sleep(0.2)
        start = time.monotonic()
        for _ in range(10):
            b.send(b'PING\n'); b.expect(b'PONG')
        assert time.monotonic()-start < 1, 'slow reader starved peer'
        slow.close(); writer.join(3); assert not failures, failures
        print('PASS: stalled output/flood does not block peer')
        b.send(b'QUIT\n'); b.expect(b'BYE'); assert b.sock.recv(1) == b''
        print('PASS: graceful quit drains output')
    finally:
        for c in clients:
            c.close()
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill(); process.wait()
        errors = process.stderr.read()
        if errors:
            print(errors, file=sys.stderr)


if __name__ == '__main__':
    run(sys.argv[1])
