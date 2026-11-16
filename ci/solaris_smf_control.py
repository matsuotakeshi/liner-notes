#!/usr/bin/python3.13 -I
"""Privileged, fixed-target SMF test lease. Installed only after explicit approval."""
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import socket
import stat
import subprocess
import sys
import time

FMRI = "svc:/application/liner-notes-ci:default"
SERVICE = "svc:/application/liner-notes-ci"
HOME = Path("/opt/liner-notes-ci")
MANIFEST = HOME / "liner-notes-ci.xml"
LOG = Path("/var/svc/log/application-liner-notes-ci:default.log")
LOCK = HOME / "session.lock"
SVCCFG = "/usr/sbin/svccfg"
SVCADM = "/usr/sbin/svcadm"
SVCS = "/usr/bin/svcs"
SVCPROP = "/usr/bin/svcprop"


def trusted(path):
    path = Path(path)
    for item in (path, *path.parents):
        info = item.lstat()
        if stat.S_ISLNK(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise ValueError("Untrusted installed SMF fixture")
    if not path.is_file():
        raise ValueError("Installed SMF fixture is not a file")


def command(args, check=True):
    # Never inherit command-search paths, Python overrides, locale or caller env.
    result = subprocess.run(args, stdin=subprocess.DEVNULL, capture_output=True,
                            text=True, timeout=35,
                            env={"PATH": "/usr/bin:/usr/sbin", "LC_ALL": "C"})
    if check and result.returncode:
        raise RuntimeError("Fixed SMF command failed: " + args[0])
    return result


def exists():
    # svcs exit 1 means fatal error, not specifically "not found". A successful
    # repository list also detects a service with no default instance yet.
    name = SERVICE.removeprefix("svc:/")
    result = command([SVCCFG, "list", name], check=False)
    if result.returncode or result.stderr:
        raise RuntimeError("Cannot determine dedicated SMF registration")
    found = result.stdout.splitlines()
    if found not in ([], [name]):
        raise RuntimeError("Unexpected dedicated SMF registration reply")
    return bool(found)


def status():
    return {"state": command([SVCS, "-H", "-o", "state", FMRI]).stdout.strip(),
            "enabled": command([SVCPROP, "-p", "general/enabled", FMRI]).stdout.strip()}


def act(action):
    mapping = {"enable": [SVCADM, "enable", FMRI],
               "disable": [SVCADM, "disable", FMRI],
               "restart": [SVCADM, "restart", FMRI],
               "clear": [SVCADM, "clear", FMRI]}
    if action == "status":
        return status()
    if action == "ping":
        return {"alive": True}
    if action == "log":
        # Only this fixture's fixed log, never a caller-provided pathname.
        fd = os.open(LOG, os.O_RDONLY | os.O_NOFOLLOW)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != 0:
                raise ValueError("Unexpected SMF log")
            os.lseek(fd, max(0, info.st_size - 32768), os.SEEK_SET)
            return {"log_tail": os.read(fd, 32768).decode("utf8", errors="replace")}
        finally:
            os.close(fd)
    if action not in mapping:
        raise ValueError("Unknown fixed SMF action")
    command(mapping[action])
    return {"requested": action}


def send(value, fd=1, clock=time.monotonic):
    # A caller keeping stdout open without reading must not stall the root
    # helper past its cleanup deadline. The fixed log reply is also bounded.
    data = (json.dumps(value, sort_keys=True) + "\n").encode()
    if len(data) > 262144:
        raise ValueError("Oversized SMF reply")
    blocking = os.get_blocking(fd)
    deadline = clock() + 10
    os.set_blocking(fd, False)
    try:
        while data:
            if clock() >= deadline:
                raise TimeoutError("SMF reply reader stalled")
            if not select.select([], [fd], [], 0.2)[1]:
                continue
            try:
                written = os.write(fd, data)
            except BlockingIOError:
                continue
            if not written:
                raise BrokenPipeError("SMF reply reader closed")
            data = data[written:]
    finally:
        os.set_blocking(fd, blocking)


def serve(fd=0, clock=time.monotonic):
    # This invocation owns registration only if the namespace was absent first.
    # Never import over, stop, delete or adopt a pre-existing service.
    if exists():
        raise ValueError("Dedicated SMF fixture already exists; administrator review required")
    command([SVCCFG, "validate", str(MANIFEST)])
    created = False
    try:
        created = True  # also clean a partially completed import, under our lease
        command([SVCCFG, "import", str(MANIFEST)])
        send({"ready": FMRI, "helper_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()})
        deadline, heartbeat = clock() + 600, clock() + 30
        pending = b""
        while clock() < deadline and clock() < heartbeat:
            if not select.select([fd], [], [], 0.5)[0]:
                continue
            data = os.read(fd, 256)
            if not data:
                if pending:
                    raise ValueError("Incomplete SMF request")
                return
            pending += data
            if len(pending) > 256:
                raise ValueError("Oversized SMF request")
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                action = line.decode("ascii", errors="strict")
                heartbeat = clock() + 30
                if action == "finish":
                    return
                send(act(action))
        raise TimeoutError("SMF test lease expired")
    finally:
        if created and exists():
            # :kill remains restricted to this service's contract. No global kill.
            command([SVCADM, "disable", "-s", "-T", "30", FMRI])
            if status()["state"] != "disabled":
                raise RuntimeError("Dedicated service did not stop; not deleting it")
            command([SVCCFG, "delete", SERVICE])
            if exists():
                raise RuntimeError("Dedicated SMF fixture cleanup incomplete")


def main():
    import fcntl
    if (sys.argv[1:] != ["session"] or sys.platform != "sunos5"
            or os.geteuid() != 0 or os.environ.get("SUDO_USER") != "ci-test"
            or socket.gethostname() != "solaris"):
        raise ValueError("Fixed Solaris CI session only")
    trusted(Path(__file__).resolve())
    trusted(MANIFEST)
    trusted(HOME / "solaris_smf_method.py")
    trusted(LOCK)
    with LOCK.open("r+") as lock:
        fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
            signal.signal(sig, lambda *_: (_ for _ in ()).throw(InterruptedError("SMF session cancelled")))
        serve()


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("SMF fixture failed: " + type(exc).__name__ + ": " + str(exc), file=sys.stderr)
        raise SystemExit(1)
