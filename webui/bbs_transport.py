"""One serialized, authenticated BBS connection per browser session.

Only the core validates BBS identities and permissions. Synchronization is an
opt-in transport extension, not a privileged login or a database interface.
"""
import asyncio
import json
import re
import secrets
import time


class BridgeError(Exception):
    pass


class TelnetDecoder:
    def __init__(self):
        self.mode = 0
        self.verb = 0
        self.sub_size = 0
        self.negotiated = set()

    def feed(self, chunk):
        data, reply = bytearray(), bytearray()
        for b in chunk:
            if self.mode == 0:
                if b == 255: self.mode = 1
                else: data.append(b)
            elif self.mode == 1:
                if b == 255: data.append(b); self.mode = 0
                elif b in (251, 252, 253, 254): self.verb = b; self.mode = 2
                elif b == 250: self.mode = 3; self.sub_size = 0
                else: self.mode = 0
            elif self.mode == 2:
                key = (self.verb, b)
                if key not in self.negotiated:
                    self.negotiated.add(key)
                    # The BBS requires server-controlled echo for hidden input.
                    # It suppresses secret bytes; exchange removes command echoes.
                    if self.verb == 251: reply.extend((255, 253 if b in (0, 1, 3) else 254, b))
                    elif self.verb == 253: reply.extend((255, 251 if b in (0, 3) else 252, b))
                self.mode = 0
            elif self.mode == 3:
                self.sub_size += 1
                if self.sub_size > 4096: raise BridgeError('Oversized Telnet control')
                if b == 255: self.mode = 4
            elif self.mode == 4:
                self.mode = 0 if b == 240 else 3
        return bytes(data), bytes(reply)


class Bridge:
    def __init__(self, host, port, *, telnet=True, timeout=30):
        self.host, self.port, self.telnet, self.timeout = host, port, telnet, timeout
        self.lock = asyncio.Lock()
        self.changed = asyncio.Event()
        self.buffer = bytearray()
        self.state = {}
        self.token = secrets.token_hex(32)
        self.closed = False
        self.writer = None
        self.reader_task = None
        self.last_browser = time.monotonic()

    async def open(self):
        try:
            self.reader, self.writer = await asyncio.wait_for(asyncio.open_connection(self.host, self.port), self.timeout)
        except OSError as error:
            raise ConnectionError('BBS connection unavailable') from error
        self.reader_task = asyncio.create_task(self._read())
        try:
            await self._until(b'RTBBS-PROBE ', include_line=True)
            await self._send('RAW')
            await self._until(b'RAW\r\n')
            await self._send('CONNECTOR OPEN ' + self.token)
            await self._until(b'CONNECTOR READY 1\r\n')
            await self.command()
        except BaseException:
            await self.close()
            raise
        return self

    async def _read(self):
        decoder = TelnetDecoder()
        try:
            while chunk := await self.reader.read(65536):
                if self.telnet:
                    chunk, reply = decoder.feed(chunk)
                    if reply: self.writer.write(reply); await self.writer.drain()
                self.buffer.extend(chunk)
                if len(self.buffer) > 2 * 1024 * 1024: raise BridgeError('BBS response exceeded limit')
                self.changed.set()
        except (OSError, BridgeError):
            pass
        finally:
            self.closed = True
            self.changed.set()

    async def _send(self, line):
        await self._send_lines([line])

    async def _send_lines(self, lines):
        """One bounded write for one operation; retain individual protocol lines."""
        if self.closed: raise BridgeError('BBS connection closed; please sign in again')
        chunks=[];total=0
        for line in lines:
            if any(c in line for c in '\r\n\0'): raise BridgeError('A command must be one line')
            raw=line.encode('utf-8')
            if len(raw)>4096: raise BridgeError('Input exceeds 4096 bytes')
            total+=len(raw)+2
            if total>100000: raise BridgeError('Connector operation exceeds limit')
            chunks.append(raw+b'\r\n')
        raw=b''.join(chunks)
        self.writer.write(raw.replace(b'\xff', b'\xff\xff') if self.telnet else raw)
        await self.writer.drain()

    async def _until(self, marker, include_line=False, timeout=None):
        async def receive():
            while True:
                at = self.buffer.find(marker)
                end = at + len(marker)
                if at >= 0 and include_line:
                    end = self.buffer.find(b'\n', end)
                    if end >= 0: end += 1
                if at >= 0 and end >= 0:
                    result = bytes(self.buffer[:end]); del self.buffer[:end]
                    return result
                if self.closed: raise BridgeError('BBS connection closed; please sign in again')
                self.changed.clear()
                await self.changed.wait()
        try:
            return await asyncio.wait_for(receive(), self.timeout if timeout is None else timeout)
        except asyncio.TimeoutError as error:
            # Never retry a possibly committed mutation after loss of its reply.
            await self.close()
            raise BridgeError('Reply timed out. Operation outcome is unknown; check before retrying.') from error

    async def exchange(self, command=None, *, timeout=None):
        """Caller owns lock. Empty string is an actual input; None is sync only."""
        sent=[]
        if command is not None:
            if len(command.encode())>4096:
                if not self.state.get('transfer_block') or not command.startswith(('FILEPUTBLOCK ','MFILEPUTBLOCK ')) or len(command)>90000:
                    raise BridgeError('Unsupported block operation')
                sent=[f'CONNECTOR BLOCK {self.token} {len(command)}']+[f'CONNECTOR DATA {self.token} '+command[at:at+3000] for at in range(0,len(command),3000)]
            else:
                line=f'CONNECTOR TRANSFER {self.token} '+command if self.state.get('editing') and self.state.get('transfer_block') and command.startswith('MFILE') else command
                sent=[line]
        request = secrets.token_hex(16)
        await self._send_lines([*sent,f'CONNECTOR SYNC {self.token} {request}'])
        prefix = f'CONNECTOR STATE {request} '.encode()
        result = await self._until(prefix, include_line=True, timeout=timeout)
        at = result.rfind(prefix)
        try: self.state = json.loads(result[at + len(prefix):])
        except (ValueError, UnicodeError) as error:
            await self.close()
            raise BridgeError('Invalid connector state') from error
        body = result[:at]
        echoed_sync = f'CONNECTOR SYNC {self.token} {request}\r\n'.encode()
        if body.endswith(echoed_sync):body=body[:-len(echoed_sync)]
        for line in sent:
            echo=line.encode()+b'\r\n'
            if body.startswith(echo):body=body[len(echo):]
        return {'text': body.decode('utf-8', errors='replace'), 'state': dict(self.state)}

    async def command(self, command=None):
        async with self.lock: return await self.exchange(command)

    async def literal(self, line):
        await self.exchange('CONNECTOR LITERAL ' + self.token)
        return await self.exchange(line)

    def idle_output(self):
        if self.lock.locked(): return ''
        end = self.buffer.rfind(b'\n') + 1
        data = bytes(self.buffer[:end]); del self.buffer[:end]
        return data.decode('utf-8', errors='replace')

    async def close(self):
        self.closed = True
        self.changed.set()
        if self.writer:
            self.writer.close()
            try: await self.writer.wait_closed()
            except OSError: pass
        if self.reader_task and self.reader_task is not asyncio.current_task():
            self.reader_task.cancel()
            try: await self.reader_task
            except asyncio.CancelledError: pass


