#!/usr/bin/python3.13 -I
"""Fixed SMF method, always executed as the nonprivileged ci-test account."""
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import sys

AREA = Path("/export/home/ci-test/ci/liner-notes-smf")


def selection():
    import pwd
    uid = pwd.getpwnam("ci-test").pw_uid
    if os.geteuid() != uid or os.geteuid() == 0:
        raise ValueError("Nonprivileged CI method required")
    if os.environ.get("SMF_FMRI") != "svc:/application/liner-notes-ci:default":
        raise ValueError("Dedicated SMF context required")
    for path in (AREA, AREA / "selection.json"):
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or info.st_uid != uid or info.st_mode & 0o077:
            raise ValueError("Private CI selection required")
    if not (AREA / "selection.json").is_file() or (AREA / "selection.json").stat().st_size > 65536:
        raise ValueError("Bounded regular selection required")
    data = json.loads((AREA / "selection.json").read_text())
    if set(data) != {"source", "sha", "config"}:
        raise ValueError("Unexpected selection fields")
    root = Path(data["source"])
    if not re.fullmatch(r"/export/home/ci-test/ci/native-solaris-[A-Za-z0-9_]+/source", str(root)):
        raise ValueError("Independent native CI source required")
    if (not re.fullmatch(r"[0-9a-f]{40}", data["sha"])
            or (root / ".ci-source-sha").read_text().strip() != data["sha"]):
        raise ValueError("Exact native source marker required")
    config = Path(data["config"])
    if config.parent != root / "build/smf-lifecycle" or config.name != "launch.json":
        raise ValueError("Private lifecycle configuration required")
    return root, config


def main():
    try:
        root, config = selection()
        # The product's real start method is exercised unchanged. Its children
        # remain in this SMF contract; neither wrapper creates a new session.
        return subprocess.run([sys.executable, "-I", str(root / "scripts/solaris_service.py"),
                               "start", str(config)], close_fds=True).returncode
    except Exception as error:
        print("SMF CI method rejected: " + type(error).__name__, file=sys.stderr)
        return 95


if __name__ == "__main__":
    raise SystemExit(main())
