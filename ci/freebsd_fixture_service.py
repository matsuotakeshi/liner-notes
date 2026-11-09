"""Bounded diagnostics and cleanup for the synthetic, private rc fixture."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import re
import stat
import shutil
import tempfile
import subprocess

LIMIT = 16384


@contextmanager
def fixture_directory(work):
    directory = Path(tempfile.mkdtemp(prefix="actual-upgrade-", dir=work))
    try:
        yield directory
    finally:
        if (directory / ".cleanup-failed").exists():
            print("Private fixture retained after incomplete cleanup: " + str(directory), flush=True)
        else:
            shutil.rmtree(directory)


def save_failure(log, artifacts, phase, reason, source_sha, rc_output=""):
    if not re.fullmatch(r"[a-z-]+", phase):
        raise ValueError("Invalid fixture phase")
    output = Path(artifacts)
    output.mkdir(exist_ok=True)
    data = b""
    try:
        fd = os.open(log, os.O_RDONLY | getattr(os, "O_NONBLOCK", 0) | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_BINARY", 0))
        with os.fdopen(fd, "rb") as stream:
            st = os.fstat(stream.fileno())
            if (not stat.S_ISREG(st.st_mode) or st.st_nlink != 1 or
                    (hasattr(os, "geteuid") and st.st_uid != os.geteuid())):
                raise ValueError("Fixture log is not regular")
            stream.seek(max(0, st.st_size - LIMIT))
            data = stream.read(LIMIT)
    except FileNotFoundError:
        pass
    destination = output / ("FREEBSD-RC-FAILURE-" + phase + ".json")
    fd = os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf8") as stream:
        json.dump({"source_sha": source_sha, "phase": phase, "reason": reason, "log_limit": LIMIT,
                   "daemon_log_tail": data.decode("utf8", errors="replace"),
                   "private_rc_output_tail": rc_output.encode("utf8")[-LIMIT:].decode("utf8", errors="replace")}, stream)
        stream.write("\n")
    return destination


class RcFixture:
    def __init__(self, rc, env, base, artifacts, source_sha, run=subprocess.run):
        self.rc, self.env = str(rc), env
        self.base, self.artifacts = Path(base), Path(artifacts)
        self.run = run
        self.source_sha = source_sha
        self.cleanup_failed = False
        self.output = ""

    def command(self, action):
        def capture(stdout, stderr):
            def text(value):
                return value.decode("utf8", errors="replace") if isinstance(value, bytes) else (value or "")
            self.output = (self.output + action + "\n" + text(stdout) + text(stderr))[-LIMIT:]
        try:
            result = self.run(["/bin/sh", self.rc, action], env=self.env,
                              capture_output=True, text=True, timeout=40)
        except subprocess.TimeoutExpired as error:
            capture(error.stdout, error.stderr)
            raise
        capture(result.stdout, result.stderr)
        return result

    def live_pids(self):
        alive = []
        for name in ("supervisor.pid", "child.pid"):
            path = self.base / name
            if path.is_symlink():
                raise ValueError("Unsafe private pid file")
            if not path.exists():
                continue
            if not path.is_file():
                raise ValueError("Unsafe private pid file")
            value = path.read_text(encoding="ascii").strip()
            if not re.fullmatch(r"[1-9][0-9]{0,9}", value):
                raise ValueError("Invalid private pid file")
            pid = int(value)
            try:
                os.kill(pid, 0)  # Read-only existence check; never signal a reused PID.
                alive.append(pid)
            except ProcessLookupError:
                pass
        return alive

    @contextmanager
    def running(self, phase):
        problem = None
        try:
            start = self.command("start")
            if start.returncode:
                raise RuntimeError("Private rc start failed")
            status = self.command("status")
            if status.returncode:
                raise RuntimeError("Private rc readiness failed")
            yield
        except BaseException as error:
            problem = error
            raise
        finally:
            # Start can fail after creating a supervisor, including a timeout.
            # Stop is attempted even when start never returned success.
            try:
                stop = self.command("stop")
                live = self.live_pids()
                if live:
                    raise RuntimeError("Private rc processes remain after stop")
                # stop=1 is normal when the failed start already cleaned up.
                if stop.returncode not in (0, 1):
                    raise RuntimeError("Private rc stop failed")
            except BaseException as error:
                self.cleanup_failed = True
                (self.base / ".cleanup-failed").touch(mode=0o600, exist_ok=True)
                if problem is None:
                    problem = error
                    raise
            finally:
                if problem is not None or self.cleanup_failed:
                    reason = "cleanup-failed" if self.cleanup_failed else type(problem).__name__
                    save_failure(self.base / "daemon.log", self.artifacts, phase, reason, self.source_sha, self.output)
