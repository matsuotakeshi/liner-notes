#!/usr/bin/env python3
"""Main-only native SMF acceptance, under native_remote's existing host lease."""
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import socket
import subprocess
import sys
import time

from solaris_smf_control import FMRI, exists
from prepare_solaris_smf import manifest

ROOT = Path(__file__).resolve().parents[1]
CONTROL = Path("/opt/liner-notes-ci/solaris_smf_control.py")
AREA = Path("/export/home/ci-test/ci/liner-notes-smf")


class Session:
    def __init__(self):
        self.child = subprocess.Popen(["/usr/bin/sudo", "-n", str(CONTROL), "session"],
                                      stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      # Separate from the build group: EOF on this
                                      # private pipe cleans SMF even if CI is killed.
                                      start_new_session=True, close_fds=True)
        self.pending = b""

    def read(self):
        deadline = time.monotonic() + 40
        while b"\n" not in self.pending:
            if time.monotonic() >= deadline:
                raise TimeoutError("SMF helper response timeout")
            if not select.select([self.child.stdout], [], [], 0.2)[0]:
                continue
            data = os.read(self.child.stdout.fileno(), 4096)
            if not data:
                raise RuntimeError("SMF helper exited before reply")
            self.pending += data
            if len(self.pending) > 65536:
                raise ValueError("SMF helper reply too large")
        line, self.pending = self.pending.split(b"\n", 1)
        return json.loads(line)

    def request(self, action):
        if action not in {"enable", "disable", "restart", "clear", "status", "ping", "log"}:
            raise ValueError("Unknown SMF action")
        self.child.stdin.write((action + "\n").encode())
        self.child.stdin.flush()
        return self.read()

    def close(self):
        self.child.stdin.close()  # EOF must disable, delete and release helper lease
        result = self.child.wait(timeout=90)
        self.child.stdout.close()
        if result:
            raise RuntimeError("SMF helper cleanup failed")


def wait_state(session, wanted, timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = session.request("status")
        if state["state"] == wanted:
            return state
        time.sleep(0.2)
    raise AssertionError("SMF did not reach " + wanted + ": " + repr(state))


def write_private(path, value):
    path.write_text(json.dumps(value) + "\n")
    path.chmod(0o600)


def verify_install():
    expected = {"solaris_smf_control.py": (ROOT / "ci/solaris_smf_control.py").read_bytes(),
                "solaris_smf_method.py": (ROOT / "ci/solaris_smf_method.py").read_bytes(),
                "liner-notes-ci.xml": manifest()}
    for name, value in expected.items():
        if (CONTROL.parent / name).read_bytes() != value:
            raise ValueError("Installed fixed SMF fixture differs from reviewed source: " + name)


def run():
    if sys.platform != "sunos5" or os.geteuid() == 0:
        raise ValueError("Non-root Solaris CI only")
    sha = (ROOT / ".ci-source-sha").read_text().strip()
    verify_install()
    work = ROOT / "build/smf-lifecycle"
    work.mkdir(mode=0o700)
    data = work / "data"
    data.mkdir(mode=0o700)
    # Fixed selection is owned by ci-test, private and serialized by native lease.
    if AREA.exists():
        if AREA.is_symlink() or AREA.stat().st_uid != os.geteuid() or AREA.stat().st_mode & 0o077:
            raise ValueError("Unsafe SMF selection directory")
    else:
        AREA.mkdir(mode=0o700)
    selection = AREA / "selection.json"
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    pidfile = work / "runtime.pid"
    launcher = work / "launch.py"
    launcher.write_text("#!" + sys.executable + "\nimport os\nfrom pathlib import Path\n" +
                        "pidfile = Path(" + repr(str(pidfile)) + ")\n" +
                        "temporary = pidfile.with_suffix('.new')\n" +
                        "temporary.write_text(str(os.getpid()))\n" +
                        "os.replace(temporary, pidfile)\n" +
                        "os.execv(" + repr(str(ROOT / "runtime")) + ", " +
                        repr([str(ROOT / "runtime"), "--bind", "127.0.0.1", str(port),
                              str(data / "notes.dat"), str(data / "members.dat")]) + ")\n")
    launcher.chmod(0o700)
    config = work / "launch.json"
    settings = {"program": str(launcher), "working_directory": str(data), "arguments": ["smf-test"]}
    write_private(config, settings)
    session = None
    selected = False
    try:
        session = Session()
        hello = session.read()
        assert hello == {"ready": FMRI, "helper_sha256": hashlib.sha256(CONTROL.read_bytes()).hexdigest()}
        # The privileged lease has finished any prior owned service before it
        # reports ready. Recover only our private selection left by CI SIGKILL.
        if selection.exists() or selection.is_symlink():
            info = selection.lstat()
            if (selection.is_symlink() or not selection.is_file() or info.st_uid != os.geteuid()
                    or info.st_mode & 0o077 or info.st_size > 65536):
                raise ValueError("Unsafe prior SMF selection")
            (work / "previous-selection.json").write_bytes(selection.read_bytes())
            (work / "previous-selection.json").chmod(0o600)
            selection.unlink()
        write_private(selection, {"source": str(ROOT), "sha": sha, "config": str(config)})
        selected = True
        assert wait_state(session, "disabled")["enabled"] == "false"
        session.request("enable")
        assert wait_state(session, "online")["enabled"] == "true"
        sys.path.insert(0, str(ROOT / "tests"))
        from test_runtime import Client
        from test_members import auth
        from test_board import post, read
        def ping(create=False):
            deadline = time.monotonic() + 30
            while True:
                try:
                    client = Client(port)
                    break
                except ConnectionRefusedError:
                    if time.monotonic() >= deadline:
                        raise
                    session.request("ping"); time.sleep(0.2)
            try:
                client.send(b"PING\n"); client.expect(b"PONG")
                auth(client, "smfprobe", create)
                if create:
                    assert post(client, "SMF retained title", "SMF retained body") == 1
                else:
                    assert "SMF retained body" in read(client, 1)[2]
            finally:
                client.close()
        ping(create=True)
        # The service cannot grant CI permission to replace its root-owned
        # method/credential metadata. Attempt an unchanged method value only.
        method = "/usr/bin/python3.13 -I /opt/liner-notes-ci/solaris_smf_method.py"
        denied = subprocess.run(["/usr/sbin/svccfg", "-s", FMRI, "setprop", "start/exec", "=",
                                 "astring:", method], capture_output=True)
        assert denied.returncode != 0, "CI unexpectedly has SMF method modification rights"
        first = int(pidfile.read_text())
        session.request("restart")
        wait_state(session, "online")
        deadline = time.monotonic() + 30
        while int(pidfile.read_text()) == first and time.monotonic() < deadline:
            session.request("ping"); time.sleep(0.2)
        assert int(pidfile.read_text()) != first
        ping()
        # Kill only the current synthetic runtime; SMF must recover its contract.
        crashed = int(pidfile.read_text())
        owner = subprocess.check_output(["/usr/bin/ps", "-p", str(crashed), "-o", "uid="], text=True).strip()
        executable = Path("/proc") / str(crashed) / "path/a.out"
        assert int(owner) == os.geteuid() and executable.resolve() == (ROOT / "runtime").resolve()
        os.kill(crashed, signal.SIGKILL)
        deadline = time.monotonic() + 45
        while int(pidfile.read_text()) == crashed and time.monotonic() < deadline:
            session.request("ping"); time.sleep(0.2)
        assert int(pidfile.read_text()) != crashed
        assert wait_state(session, "online")["enabled"] == "true"
        ping()
        session.request("disable")
        assert wait_state(session, "disabled")["enabled"] == "false"
        # Configuration failure enters maintenance; it must not appear online.
        write_private(config, {**settings, "program": str(work / "absent")})
        session.request("enable")
        wait_state(session, "maintenance")
        write_private(config, settings)
        session.request("clear")
        wait_state(session, "online")
        ping()
        log = session.request("log")["log_tail"]
        assert "LISTEN 127.0.0.1 " in log and "Liner Notes SMF:" in log
        (work / "service.log").write_text(log)
        print("SMF_LOG " + json.dumps(log))
    finally:
        if session is not None:
            session.close()
        if selected:
            selection.unlink()
    assert not exists(), "SMF fixture registration was not removed"
    print("PASS SMF: private registration/start/stop/restart, crash recovery, retained enable state, maintenance recovery and cleanup; OS boot not tested")


if __name__ == "__main__":
    run()
