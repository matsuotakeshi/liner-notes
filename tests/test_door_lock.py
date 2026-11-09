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
    for mode in ([], ["-DLINER_TEST_POSIX_DOOR_LOCK=1"]):
        executable = Path(tmp) / ("record" if mode else "native")
        command = shlex.split(os.environ.get("CC", "cc")) + shlex.split(os.environ.get("CFLAGS", ""))
        subprocess.run(command + ["-std=c11", "-Wall", "-Wextra", "-Werror", *mode,
                                  "-I" + str(ROOT / "src"), str(ROOT / "tests/test_door_lock.c"),
                                  str(ROOT / "src/door_lock.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable), str(Path(tmp) / "lock")], check=True, timeout=30)
