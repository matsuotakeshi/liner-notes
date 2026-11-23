"""Owned launch lifecycle for the Solaris Door supervisor candidate."""

from contextlib import ExitStack, contextmanager
from dataclasses import dataclass
import os
from pathlib import Path
import re
import select
import struct
import subprocess
import tempfile
import time

from door_broker import secure_path
from door_solaris_admission import admit


_PREPARED = b"PREPARED\n"
_ENV = {"PATH": "/usr/bin:/usr/sbin", "LC_ALL": "C"}


def _directional_channel(state_dir):
    """Open separate read/write descriptors and leave no reachable FIFO name."""
    with ExitStack() as descriptors:
        with tempfile.TemporaryDirectory(prefix="door-fifo-", dir=state_dir) as directory:
            path = Path(directory) / "channel"
            os.mkfifo(path, 0o600)
            flags = os.O_NONBLOCK | os.O_NOFOLLOW
            reader = os.open(path, os.O_RDONLY | flags)
            descriptors.callback(os.close, reader)
            writer = os.open(path, os.O_WRONLY | flags)
            descriptors.callback(os.close, writer)
            path.unlink()
            os.set_blocking(reader, True)
            os.set_blocking(writer, True)
        descriptors.pop_all()  # Ownership passes to the caller.
        return reader, writer


def _await_prepared(fd, holder):
    until = time.monotonic() + 8
    data = b""
    while True:
        remaining = until - time.monotonic()
        if remaining <= 0 or not select.select([fd], [], [], remaining)[0]:
            raise TimeoutError("Solaris Door boundary readiness deadline")
        piece = os.read(fd, 32)
        if not piece:
            raise OSError("Solaris Door child closed the status channel before setup")
        data += piece
        if not _PREPARED.startswith(data):
            raise OSError("Solaris Door child rejected startup")
        if data == _PREPARED:
            if holder.poll() is not None:
                raise OSError("Solaris Door contract already ended")
            return


def _stop_owned_contract(holder):
    if holder is not None:
        if holder.poll() is None:
            holder.kill()
        holder.wait(timeout=5)


@dataclass(frozen=True)
class PreparedDoor:
    """Borrowed handles for the trusted supervisor before game-ready handoff."""

    master: int
    status: int
    contract: subprocess.Popen
    issuer: object


@contextmanager
def launch_prepared(root, state_dir, uid, helper, columns, rows, open_broker):
    """Reserve, start and stop one owned contract.

    The trusted caller obtains a socket and ticket from the Broker through
    open_broker. The caller must separately confirm game readiness and watch
    the BBS and Broker connections before relaying the PTY.
    """
    import fcntl
    import pty
    import termios

    executable = secure_path(helper, 0)
    with admit(root, state_dir, uid) as (root_fd, slot), ExitStack() as parent:
        holder = issuer = None
        try:
            issuer, token = open_broker()
            if not isinstance(token, str) or not re.fullmatch(r"[0-9a-f]{64}", token):
                raise ValueError("Invalid Door Broker reply")
            ticket_bytes = token.encode("ascii") + b"\n"
            with ExitStack() as inherited:
                master, slave = pty.openpty()
                parent.callback(os.close, master)
                inherited.callback(os.close, slave)
                fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))

                ticket, writer = _directional_channel(state_dir)
                inherited.callback(os.close, ticket)
                try:
                    if os.write(writer, ticket_bytes) != len(ticket_bytes):
                        raise OSError("Incomplete Door ticket pipe write")
                finally:
                    os.close(writer)

                status, child_status = _directional_channel(state_dir)
                parent.callback(os.close, status)
                inherited.callback(os.close, child_status)
                argv = ("/usr/bin/ctrun", "-l", "contract", "-o", "noorphan",
                        str(executable), str(uid), str(root_fd), str(slave),
                        str(ticket), str(child_status))
                holder = subprocess.Popen(
                    argv, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL, env=_ENV,
                    pass_fds=(root_fd, slave, ticket, child_status),
                )
            _await_prepared(status, holder)
            yield PreparedDoor(master, status, holder, issuer)
        finally:
            try:
                if issuer is not None:
                    issuer.close()
            except BaseException:
                _stop_owned_contract(holder)
                raise
            slot.stop_and_release(lambda: _stop_owned_contract(holder), settle=2)
