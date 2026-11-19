"""Run the OpenSSH smoke test on a real local terminal, with bounded diagnostics."""
import os
import subprocess
import tempfile
import time


def pty_ping(command, timeout=12):
    import errno
    import pty
    import select
    import termios
    import tty

    master, slave = pty.openpty()
    child = None
    output = b''
    pending = b''
    phase = 'start'
    failure = None
    cleanup = 'already exited'
    with tempfile.TemporaryFile() as errors:
        try:
            tty.setraw(slave)
            termios.tcsetwinsize(slave, (24, 80))
            assert os.isatty(slave) and termios.tcgetwinsize(slave) == (24, 80)
            # stdin/stdout are a terminal, stderr remains separately readable.
            # Avoid preexec_fn: the SSH test already has Paramiko threads.
            child = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=errors,
                                     start_new_session=True, env={**os.environ, 'TERM': 'xterm'})
            os.close(slave)
            slave = None

            def until(marker):
                nonlocal output, pending
                deadline = time.monotonic() + timeout
                while marker not in pending:
                    left = deadline - time.monotonic()
                    if left <= 0 or not select.select([master], [], [], max(0, left))[0]:
                        raise TimeoutError(f'waiting for {marker!r}')
                    try:
                        data = os.read(master, 8192)
                    except OSError as exc:
                        if exc.errno != errno.EIO:
                            raise
                        data = b''
                    if not data:
                        raise EOFError(f'terminal closed before {marker!r}')
                    output = (output + data)[-65536:]
                    pending = (pending + data)[-65536:]
                end = pending.index(marker) + len(marker)
                result, pending = pending[:end], pending[end:]
                return result

            phase = 'initial BBS prompt'
            until(b'] > ')
            phase = 'PING response'
            os.write(master, b'PING\r')
            response = until(b'] > ')
            assert b'PONG' in response, 'BBS response did not contain PONG'
            phase = 'complete'
        except Exception as exc:
            failure = f'{type(exc).__name__}: {exc}'
        finally:
            if slave is not None:
                os.close(slave)
            before_cleanup = child.poll() if child else None
            if child:
                if before_cleanup is None:
                    cleanup = 'test SIGTERM'
                    child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    cleanup = 'test SIGKILL'
                    child.kill()
                    child.wait(timeout=5)
            os.close(master)
            errors.seek(0, os.SEEK_END)
            errors.seek(max(0, errors.tell() - 4096))
            diagnostics = {'phase': phase, 'error': failure,
                           'exit_before_cleanup': before_cleanup,
                           'returncode': child.returncode if child else None,
                           'cleanup': cleanup, 'stderr': errors.read(),
                           'terminal_tail': output[-1500:]}
            print('OpenSSH PTY diagnostics:', diagnostics, flush=True)
    assert failure is None, diagnostics
    return diagnostics
