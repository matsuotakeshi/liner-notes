"""POSIX ZMODEM process adapter. No BBS, account, or vault-file dependencies.

Use audited lrzsz 0.13.1 executables as separate programs, never a shell.
The caller supplies a private staging directory, transport FDs and a live
authorization callback. File contents never share the diagnostic channel.
"""
import select
import contextlib
import os
import pathlib
import re
import shutil
import signal
import stat
import subprocess
import tempfile
import time

def configured_limit(key,maximum):
    value=int(os.environ.get('LINER_LIMIT_'+key,str(maximum)))
    if not 1<=value<=maximum:raise ValueError('Invalid transfer limit: '+key)
    return value
MAX_BYTES = configured_limit('FILE',1024 * 1024 * 1024)
MAX_BATCH_BYTES = configured_limit('BATCH_BYTES',16 * 1024 * 1024 * 1024)
MAX_BATCH_FILES = configured_limit('BATCH_FILES',16)


class TransferFailure(ValueError):
    def __init__(self, message, code="TRANSFER"):
        super().__init__(message)
        self.code = code


def safe_name(name):
    try:
        encoded = name.encode('utf-8')
    except UnicodeError as error:
        raise TransferFailure('Invalid UTF-8 filename') from error
    if (not encoded or len(encoded) > 100 or name.startswith('.') or
            any(c in name for c in '/\\:') or
            any(ord(c) < 32 or 127 <= ord(c) <= 159 for c in name)):
        raise TransferFailure('Invalid filename')
    return name


def staged_file(directory, required=False):
    """Reject a batch, paths, links, devices, or oversized output before import."""
    entries = list(pathlib.Path(directory).iterdir())
    if len(entries) > 1 or (required and len(entries) != 1):
        raise TransferFailure('Exactly one file is required')
    if not entries:
        return None
    path = entries[0]
    safe_name(path.name)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
        raise TransferFailure('Staging entry is not a private regular file')
    if info.st_size > MAX_BYTES:
        raise TransferFailure('File exceeds configured limit')
    return path


def staged_files(directory, required=False):
    entries=list(pathlib.Path(directory).iterdir())
    if len(entries)>MAX_BATCH_FILES or (required and not entries):raise TransferFailure('Batch requires 1..16 files')
    total=0
    for path in entries:
        safe_name(path.name);info=path.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_nlink!=1 or info.st_size>MAX_BYTES:
            raise TransferFailure('Invalid batch file')
        total+=info.st_size
    if total>MAX_BATCH_BYTES:raise TransferFailure('Batch exceeds 16 GiB')
    return sorted(entries)


@contextlib.contextmanager
def raw_terminal(fd):
    """Restore terminal flags even after timeout, failed authentication or signals."""
    import termios
    import tty
    saved = termios.tcgetattr(fd) if os.isatty(fd) else None
    try:
        if saved is not None:
            tty.setraw(fd, termios.TCSANOW)
        yield
    finally:
        if saved is not None:
            termios.tcflush(fd, termios.TCIFLUSH)
            termios.tcsetattr(fd, termios.TCSANOW, saved)


def child_limits():
    import resource
    os.umask(0o077)
    for kind, value in ((resource.RLIMIT_FSIZE, MAX_BYTES),
                        (resource.RLIMIT_CORE, 0), (resource.RLIMIT_CPU, 60)):
        _, hard = resource.getrlimit(kind)
        value = value if hard == resource.RLIM_INFINITY else min(value, hard)
        resource.setrlimit(kind, (value, value))


class TransferRelay:
    """Bounded, nonblocking wire relay; activity means bytes actually moved.

    Do not count authorization polling or a merely writable socket as progress.
    Drain the engine's final handshake before returning the terminal to the BBS.
    """
    def __init__(self, proc, input_fd, output_fd):
        self.proc=proc;self.input=input_fd;self.output=output_fd
        self.to_engine=bytearray();self.to_peer=bytearray();self.eof=False;self.peer_eof=False
        self.flags={};self.input_closed=False;self.child_input=proc.stdin.fileno()
        self.flags={fd:os.get_blocking(fd) for fd in {input_fd,output_fd,proc.stdin.fileno(),proc.stdout.fileno()}}
        for fd in self.flags:os.set_blocking(fd,False)
    def step(self):
        ci,co=self.child_input,self.proc.stdout.fileno()
        live=self.proc.poll() is None
        reads=([co] if not self.eof and len(self.to_peer)<65536 else [])
        if live and not self.input_closed and not self.peer_eof and len(self.to_engine)<65536:reads.append(self.input)
        writes=([ci] if live and not self.input_closed and self.to_engine else [])+([self.output] if self.to_peer else [])
        ready_r,ready_w,_=select.select(reads,writes,[],.02)
        activity=False
        for fd in ready_r:
            try:data=os.read(fd, min(4096,65536-len(self.to_peer if fd==co else self.to_engine)))
            except BlockingIOError:continue
            if fd==co:
                if not data:self.eof=True
                self.to_peer.extend(data)
            else:
                if not data:self.peer_eof=True
                self.to_engine.extend(data)
                activity=activity or bool(data)
        for fd in ready_w:
            buffer=self.to_peer if fd==self.output else self.to_engine
            try:n=os.write(fd,buffer)
            except BlockingIOError:continue
            except BrokenPipeError:
                if fd==ci:buffer.clear();continue
                raise TransferFailure('Peer disconnected')
            del buffer[:n]
            if fd==self.output and n:activity=True
        if self.peer_eof and not self.to_engine and not self.input_closed:
            # A sender can close immediately after its final handshake. Deliver
            # buffered bytes and EOF; the engine receipt decides success.
            self.flags.pop(ci,None);self.proc.stdin.close();self.input_closed=True
        return activity
    def done(self):return self.proc.poll() is not None and self.eof and not self.to_peer
    def close(self):
        for fd,blocking in self.flags.items():
            try:os.set_blocking(fd,blocking)
            except OSError:pass
        self.proc.stdin.close();self.proc.stdout.close()


class ZmodemProcess:
    """A transfer engine can be replaced without changing publication workflows."""
    def __init__(self, receiver='lrz', sender='lsz', timeout=120, process_group=True, protocol='z', filename=None, expected_size=None, x_variant='x'):
        if os.name != 'posix':
            raise TransferFailure('ZMODEM entry requires a POSIX host')
        if not 1 <= timeout <= 3600:
            raise TransferFailure('Timeout must be 1..3600 seconds')
        self.receiver = receiver
        self.sender = sender
        self.timeout = timeout
        self.process_group = process_group
        if protocol not in ('z','x','y'):raise TransferFailure('Unknown process protocol')
        self.x_variant=x_variant;self.protocol=protocol;self.filename=filename;self.expected_size=expected_size

    @staticmethod
    def executable(name):
        path = shutil.which(str(name))
        if not path:
            raise TransferFailure('lrzsz executable was not found')
        path = str(pathlib.Path(path).resolve())
        # Version output is local administrator-selected executable data.
        result = subprocess.run([path, '--version'], capture_output=True,
                                timeout=5, env={'PATH': os.defpath, 'LC_ALL': 'C'})
        if result.returncode or not re.search(rb'\((?:GNU )?lrzsz\) 0\.13\.1\b', result.stdout):
            raise TransferFailure('This prototype requires lrzsz 0.13.1')
        return path

    def _run(self, executable, arguments, directory, input_fd, output_fd, authorize, receive, expected_files=1, resume=False, completed=None, paths=None):
        executable = self.executable(executable)
        directory = pathlib.Path(directory).resolve()
        authorize()
        # Do not inherit engine options, journal paths, loader overrides or
        # credentials from the parent environment.
        env = {'PATH': os.defpath, 'HOME': str(directory), 'LC_ALL': 'C',
               'ZMODEM_RESTRICTED': '1'}
        # lrz Restricted=2 forbids every existing file, including resume.
        # Its default Restricted=1 still rejects absolute/.. paths and uses
        # O_NOFOLLOW for every component. Only private single-file resume uses it.
        if resume:del env['ZMODEM_RESTRICTED']
        started = time.monotonic()
        last_progress = started
        received_sizes = ()
        next_check = started
        proc = None
        relay = None
        transfer_complete = False
        with tempfile.TemporaryDirectory(prefix='liner-zmodem-log-') as logs, raw_terminal(input_fd):
            journal = pathlib.Path(logs) / 'engine.log'
            notified=set()
            def report_completed():
                if not completed or not journal.exists():return
                if journal.stat().st_size>65536:raise TransferFailure('Oversized engine journal')
                data=journal.read_bytes();rows=data.split(b'\n')[:-1]
                records=[r for r in rows if (' proto='+self.protocol+' filename=').encode() in r]
                if len(records)>len(paths):raise TransferFailure('Unexpected transfer receipt')
                for i,row in enumerate(records):
                    if i not in notified and row.rsplit(b' status=',1)[-1].split(b' ',1)[0]==b'ok':
                        completed(paths[i]);notified.add(i)
            try:
                proc = subprocess.Popen(
                    [executable, {'z':'--zmodem','x':'--xmodem','y':'--ymodem'}[self.protocol], '--binary'] + ([] if resume else ['--restricted']) + [
                     '--quiet', '--escape', '--syslog=off', '--journal', str(journal)] + arguments,
                    cwd=directory, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL, env=env, close_fds=True,
                    start_new_session=self.process_group, preexec_fn=child_limits)
                relay=TransferRelay(proc,input_fd,output_fd)
                while not relay.done():
                    if relay.step():last_progress=time.monotonic()
                    now = time.monotonic()
                    if receive:
                        (staged_files if expected_files==0 else staged_file)(directory)
                        sizes = tuple(sorted((p.name, p.stat().st_size) for p in directory.iterdir()))
                        if sizes != received_sizes:
                            received_sizes = sizes
                            last_progress = now
                    if now - last_progress >= self.timeout:
                        raise TransferFailure('ZMODEM transfer timed out', 'TIMEOUT')
                    if journal.exists() and journal.stat().st_size > 65536:
                        raise TransferFailure('Oversized engine journal')
                    if now >= next_check:
                        authorize()
                        next_check = time.monotonic() + 1
                    report_completed()
                report_completed()
                if proc.returncode:
                    raise TransferFailure('ZMODEM peer cancelled or transfer failed')
                # Exit zero can also mean that the peer skipped an existing file.
                # Require every expected ZMODEM file to succeed, excluding skipped files
                # rsplit prevents filename text spoofing the status.
                if not journal.exists() or journal.stat().st_size > 65536:
                    raise TransferFailure('Missing or oversized engine journal')
                self.last_journal=journal.read_bytes()
                rows = self.last_journal.splitlines()
                if receive and expected_files==0:expected_files=len(staged_files(directory,required=True))
                # lrzsz adds a session summary after multi-file transfers.
                # Accept it only when the completed count matches exactly.
                if expected_files > 1:
                    marker = (' proto='+self.protocol+' files_ok=' + str(expected_files) + ' ').encode()
                    if len(rows) != expected_files + 1 or marker not in rows[-1] or b' filename=' in rows[-1]:
                        raise TransferFailure('Incomplete ZMODEM batch summary')
                    rows = rows[:-1]
                if (len(rows) != expected_files or any((' proto='+self.protocol+' filename=').encode() not in row or row.rsplit(b' status=', 1)[-1].split(b' ', 1)[0] != b'ok' for row in rows)):
                    raise TransferFailure('All expected ZMODEM files must complete')
                authorize()
                if receive:
                    if expected_files>1:staged_files(directory,required=True)
                    else:staged_file(directory, required=True)
                transfer_complete = True
            finally:
                if proc is not None and proc.poll() is None:
                    try:
                        (os.killpg if self.process_group else os.kill)(proc.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    try:
                        proc.wait(timeout=1)
                    except subprocess.TimeoutExpired:
                        try:
                            (os.killpg if self.process_group else os.kill)(proc.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        proc.wait()
                if relay is not None:
                    if not transfer_complete:
                        # The old direct wire let lrzsz emit its abort bytes on
                        # termination. Preserve that behavior through the relay.
                        relay.to_peer.extend(b'\x18'*8+b'\x08'*8)
                        until=time.monotonic()+1
                        try:
                            while not relay.done() and time.monotonic()<until:relay.step()
                        except (OSError,ValueError):pass
                    relay.close()
                report_completed()

    def receive(self, directory, input_fd, output_fd, authorize):
        if list(pathlib.Path(directory).iterdir()):
            raise TransferFailure('Receive directory must be empty')
        args=['--protect']
        if self.protocol=='x':
            if self.filename is None or self.expected_size is None or not 0<=self.expected_size<=MAX_BYTES:raise TransferFailure('XMODEM requires filename and exact size')
            args+=([] if self.x_variant=='s' else ['--with-crc'])+['--',safe_name(self.filename)]
        self._run(self.receiver, args, directory, input_fd, output_fd, authorize, True)
        path=staged_file(directory, required=True)
        if self.protocol=='x':
            # XMODEM has no size field. Never guess by stripping trailing 0x1a.
            size=path.stat().st_size
            if not self.expected_size<=size<=self.expected_size+1023:raise TransferFailure('XMODEM size mismatch')
            with path.open('r+b') as f:
                f.seek(self.expected_size);padding=f.read()
                if any(x not in (0,26) for x in padding):raise TransferFailure('XMODEM padding mismatch')
                f.truncate(self.expected_size);f.flush();os.fsync(f.fileno())
        return path

    def receive_many(self,directory,input_fd,output_fd,authorize):
        if list(pathlib.Path(directory).iterdir()):raise TransferFailure('Receive directory must be empty')
        self._run(self.receiver,['--protect'],directory,input_fd,output_fd,authorize,True,expected_files=0)
        return staged_files(directory,required=True)

    def _verify_resume_receiver(self):
        import json,hashlib
        executable=pathlib.Path(self.executable(self.receiver))
        marker=pathlib.Path(os.environ.get('LINER_NOTES_RESUME_RECEIPT',str(executable.with_name(executable.name+'.liner-notes-resume.json'))))
        try:verified=json.loads(marker.read_text())
        except (OSError,ValueError) as error:raise TransferFailure('Build the stage56 CRC-corrected receiver before resuming', 'RECEIVER') from error
        if verified!={'patch':'crc32-lp64-v1','sha256':hashlib.sha256(executable.read_bytes()).hexdigest()}:
            raise TransferFailure('Resume receiver does not match its build receipt', 'RECEIVER')

    def receive_resume(self,directory,input_fd,output_fd,authorize):
        staged_file(directory,required=True);self._verify_resume_receiver()
        # Double -r compares the retained prefix by ZCRC before requesting data.
        self._run(self.receiver,['--resume','--resume'],directory,input_fd,output_fd,authorize,True,resume=True)
        return staged_file(directory,required=True)

    def receive_many_resume(self,directory,input_fd,output_fd,authorize):
        staged_files(directory,required=True);self._verify_resume_receiver()
        self._run(self.receiver,['--resume','--resume'],directory,input_fd,output_fd,authorize,True,expected_files=0,resume=True)
        return staged_files(directory,required=True)

    def send(self, path, input_fd, output_fd, authorize):
        path = pathlib.Path(path).resolve()
        safe_name(path.name)
        if staged_file(path.parent, required=True) != path:
            raise TransferFailure('Unexpected sending file')
        self._run(self.sender, (['--1k'] if self.protocol=='x' and self.x_variant=='k' else [])+['--protect', '--', path.name], path.parent,
                  input_fd, output_fd, authorize, False)

    def send_many(self, paths, input_fd, output_fd, authorize, completed=None):
        if not 1 <= len(paths) <= MAX_BATCH_FILES:
            raise TransferFailure('Batch requires 1..16 files')
        paths=[pathlib.Path(path) for path in paths]
        directory=paths[0].parent.resolve()
        for path in paths:
            safe_name(path.name)
            if path.is_symlink() or not path.is_file() or path.parent.resolve()!=directory:
                raise TransferFailure('Invalid batch source')
        if len(set(path.name for path in paths))!=len(paths) or set(directory.iterdir())!=set(paths):
            raise TransferFailure('Unexpected or duplicate batch file')
        if any(p.stat().st_size>MAX_BYTES for p in paths) or sum(p.stat().st_size for p in paths)>MAX_BATCH_BYTES:
            raise TransferFailure('Configured batch capacity exceeded')
        self._run(self.sender,['--protect','--']+[path.name for path in paths],directory,
                  input_fd,output_fd,authorize,False,expected_files=len(paths),completed=completed,paths=paths)
