"""SSH test screen reader; no SSH dependency for deterministic stream tests."""
import time


MAIL_PROMPT = b'Mail (L list/N date/T newest/E send/R reply/K delete/number/+/0 exit)> '


class Screen:
    def __init__(self, channel):
        self.channel = channel
        self.buf = b''
        channel.settimeout(12)

    def until(self, marker=b'] > '):
        deadline = time.monotonic() + 12
        while marker not in self.buf:
            assert time.monotonic() < deadline, ('missing marker', marker, self.buf[-1500:])
            try:
                data = self.channel.recv(8192)
            except TimeoutError as exc:
                raise AssertionError(('missing marker', marker, self.buf[-1500:])) from exc
            assert data, ('unexpected EOF', self.buf[-1500:])
            self.buf += data
        end = self.buf.index(marker) + len(marker)
        out, self.buf = self.buf[:end], self.buf[end:]
        return out

    def cmd(self, text, marker=b'] > '):
        self.channel.sendall(text.encode() + b'\r')
        return self.until(marker)
