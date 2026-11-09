"""Exercise the Solaris record-lock holder on a hosted POSIX machine too."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
if os.name != "posix":
    raise SystemExit("This native lease test requires a POSIX host")
with tempfile.TemporaryDirectory(prefix="door-lock-") as tmp:
    command = shlex.split(os.environ.get("CC", "cc")) + shlex.split(os.environ.get("CFLAGS", ""))
    helper = Path(tmp) / "liner-notes-door-lock"
    subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                              str(ROOT / "src/door_lock_helper.c"), "-o", str(helper)], check=True)
    for mode in ([], ["-DLINER_TEST_POSIX_DOOR_LOCK=1"]):
        executable = Path(tmp) / ("record" if mode else "native")
        command = shlex.split(os.environ.get("CC", "cc")) + shlex.split(os.environ.get("CFLAGS", ""))
        subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror", *mode,
                                  "-I" + str(ROOT / "src"), str(ROOT / "tests/test_door_lock.c"),
                                  str(ROOT / "src/door_lock.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable), str(Path(tmp) / "lock")], check=True, timeout=30)

# Real exec-image regression on Linux hosted CI: a 1 GiB owner must not leave
# that virtual address space in its long-lived holder. No swap tuning required.
if os.path.isdir("/proc/self/task"):
    import time
    with tempfile.TemporaryDirectory(prefix="door-large-owner-") as tmp:
        directory = Path(tmp)
        helper = directory / "liner-notes-door-lock"
        command = shlex.split(os.environ.get("CC", "cc")) + shlex.split(os.environ.get("CFLAGS", ""))
        subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                  str(ROOT / "src/door_lock_helper.c"), "-o", str(helper)], check=True)
        owner = directory / "owner"
        subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror",
                                  "-DLINER_TEST_POSIX_DOOR_LOCK=1", "-DLINER_TEST_LARGE_OWNER=1",
                                  "-I" + str(ROOT / "src"), str(ROOT / "tests/test_door_lock.c"),
                                  str(ROOT / "src/door_lock.c"), "-o", str(owner)], check=True)
        process = subprocess.Popen([str(owner), "large-owner", str(directory / "lock")],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            assert process.stdout.readline() == "READY\n"
            children = Path(f"/proc/{process.pid}/task/{process.pid}/children").read_text().split()
            assert len(children) == 1, children
            child = int(children[0])
            assert Path(f"/proc/{child}/exe").resolve() == helper.resolve()
            def vmsize(pid):
                return int(next(line for line in Path(f"/proc/{pid}/status").read_text().splitlines()
                                if line.startswith("VmSize:")).split()[1])
            assert vmsize(process.pid) >= 1024 * 1024
            assert vmsize(child) < 128 * 1024, "Holder retained the owner's huge address space"
            process.stdin.close()
            assert process.wait(timeout=5) == 0
            assert not Path(f"/proc/{child}").exists(), "Holder was not reaped"
            # Installed layout: executable in sbin, helper in ../libexec.
            (directory / "sbin").mkdir(); (directory / "libexec").mkdir()
            installed = directory / "sbin/liner-notes"
            owner.rename(installed); helper.rename(directory / "libexec/liner-notes-door-lock")
            result = subprocess.run([str(installed), "large-owner", str(directory / "installed-lock")],
                                    input="", capture_output=True, text=True, timeout=5)
            assert result.returncode == 0 and result.stdout == "READY\n", result
            (directory / "libexec").chmod(0o777)
            result = subprocess.run([str(installed), "large-owner", str(directory / "unsafe-parent-lock")],
                                    input="", capture_output=True, text=True, timeout=5)
            assert result.returncode != 0 and not (directory / "unsafe-parent-lock").exists()
            (directory / "libexec").chmod(0o755)
            for mode in (0o666, 0o775, 0o4755):
                (directory / "libexec/liner-notes-door-lock").chmod(mode)
                result = subprocess.run([str(installed), "large-owner", str(directory / "unsafe-lock")],
                                        input="", capture_output=True, text=True, timeout=5)
                assert result.returncode != 0 and not (directory / "unsafe-lock").exists()
            safe_helper = directory / "libexec/liner-notes-door-lock"
            safe_helper.chmod(0o755)
            result = subprocess.run([str(installed), "exec-fail", str(safe_helper), str(directory / "exec-fail-lock")],
                                    capture_output=True, text=True, timeout=5)
            assert result.returncode == 0 and result.stdout == "EXEC FAILURE REAPED\n", result
            print("PASS tiny holder exec image: 1 GiB owner, <128 MiB helper, EOF/reap, installed path and unsafe-mode/ancestor rejection")
        finally:
            if process.poll() is None:
                if process.stdin and not process.stdin.closed: process.stdin.close()
                try: process.wait(timeout=5)
                except subprocess.TimeoutExpired: process.kill(); process.wait()
