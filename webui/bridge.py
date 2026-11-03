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
        if self.closed: raise BridgeError('BBS connection closed; please sign in again')
        if any(c in line for c in '\r\n\0'): raise BridgeError('A command must be one line')
        raw = line.encode('utf-8')
        if len(raw) > 4096: raise BridgeError('Input exceeds 4096 bytes')
        raw += b'\r\n'
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
                for line in sent:await self._send(line)
            else:
                line=f'CONNECTOR TRANSFER {self.token} '+command if self.state.get('editing') and self.state.get('transfer_block') and command.startswith('MFILE') else command
                sent=[line]
                await self._send(line)
        request = secrets.token_hex(16)
        await self._send(f'CONNECTOR SYNC {self.token} {request}')
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


def response_data(text):
    """Interpret documented raw records. Never turn user content into HTML."""
    lines = text.splitlines()
    data = {'boards': [], 'notes': [], 'mail': [], 'profiles': [], 'files': [], 'file_boards': [], 'tree': [], 'matches': [], 'next': 0}
    for line in lines:
        m=re.fullmatch(r'VBOARD ([01]) (\S+)(?: \| (.*))?',line)
        if m:data['file_boards'].append({'upload':m[1]=='1','path':m[2],'title':m[3] or m[2]})
        m = re.fullmatch(r'FILEITEM (\d+) (\d+) (.*)', line)
        if m: data['files'].append({'id':int(m[1]),'size':int(m[2]),'name':m[3]})
        m = re.fullmatch(r'FILEDETAIL (\d+) (ASCII|BINARY) (\d+) (.*)', line)
        if m:
            for item in data['files']:
                if item['id']==int(m[1]):item.update(attribute=m[2],downloads=int(m[3]),description=m[4])
        m = re.fullmatch(r'\s*#(\d+) parent=(\d+) depth=(\d+) write=(yes|no) unread=(\d+|-) (.*)', line)
        if m: data['boards'].append(dict(zip(('id','parent','depth','write','unread','title'),m.groups())))
        m = re.fullmatch(r'#(\d+) replies=(\d+) latest=(\d+) updated=(\S+) unread=(\d+|-) (.*)', line)
        if m: data['notes'].append(dict(zip(('id','replies','latest','updated','unread','title'),m.groups())))
        m = re.fullmatch(r'MAIL ROW (\d+) (\S+) (\S+) ([01]) (.*)', line)
        if m: data['mail'].append(dict(zip(('id','sender','recipient','read','title'),m.groups())))
        m = re.fullmatch(r'(?:MAIL )?END NEXT (\d+)', line)
        if m: data['next'] = int(m[1])
        m=re.fullmatch(r'\s*#(\d+) parent=(\d+) depth=(\d+) (.*)',line)
        if m and text.startswith('TREE '):data['tree'].append(dict(zip(('id','parent','depth','title'),m.groups())))
        m=re.fullmatch(r'(\d+) (\d+) (\d+) (.*)',line)
        if m and text.startswith(('FIND ','NEWS ')):data['matches'].append(dict(zip(('id','board','parent','title'),m.groups())))
    m = re.search(r'(?:^|\r\n)ARTICLE (\d+) PARENT (\d+)([^\r\n]*)\r\nTITLE ([^\r\n]*)\r\nBODY (\d+)\r\n',text)
    if m:
        body = text[m.end():].encode('utf-8')[:int(m[5])].decode('utf-8')
        data['article'] = {'id':int(m[1]),'parent':int(m[2]),'meta':m[3].strip(),'title':m[4],'body':body}
    m = re.search(r'(?:^|\r\n)MAIL ITEM (\d+) FROM (\S+) TO (\S+) READ (\d+) REPLY (\d+)\r\nTITLE ([^\r\n]*)\r\nBODY\r\n',text)
    if m:
        end = text.rfind('MAIL END\r\n')
        data['message'] = {'id':int(m[1]),'sender':m[2],'recipient':m[3],'read':int(m[4]),'parent':int(m[5]),'title':m[6],'body':text[m.end():end]}
    if text.startswith('PROFILES '):
        for line in lines[1:]:
            m=re.fullmatch(r'(\d+) (\S+) (.*)',line)
            if m:data['profiles'].append({'id':int(m[1]),'login':m[2],'handle':m[3]})
    m=re.match(r'PROFILE (\d+) (\S+)\r\n',text)
    if m:
        profile={'id':int(m[1]),'login':m[2],'fields':{}}
        for line in lines[1:]:
            if line.startswith(('PROFILEBODY ','END PROFILE')):break
            key,_,value=line.partition(' ')
            if key:profile['fields'][key]=value
        bio=re.search(r'\r\nPROFILEBODY (\d+)\r\n',text)
        if bio:profile['fields']['BIO']=text[bio.end():].encode()[:int(bio[1])].decode()
        data['profile']=profile
    return data
