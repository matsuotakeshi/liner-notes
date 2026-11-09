#!/usr/bin/env python3
"""SMF contract start method; JSON arguments are never interpreted by a shell."""
import argparse
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import time


def settings(path):
    path = Path(path)
    info = path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_mode & 0o022:
        raise ValueError("SMF configuration must be a regular file, not group/world writable")
    if info.st_size > 65536:
        raise ValueError("SMF configuration is too large")
    config = json.loads(path.read_text(encoding="utf8"))
    if not isinstance(config, dict) or set(config) != {"program", "working_directory", "arguments"}:
        raise ValueError("Expected program, working_directory and arguments")
    for key in ("program", "working_directory"):
        value = config[key]
        if not isinstance(value, str) or not value.startswith("/") or "\0" in value:
            raise ValueError("Program and working directory must be absolute paths")
    args = config["arguments"]
    if not isinstance(args, list) or not args or len(args) > 512 or any(
            not isinstance(arg, str) or "\0" in arg or len(arg) > 4096 for arg in args):
        raise ValueError("Arguments must be a nonempty array of bounded strings")
    if not os.path.isfile(config["program"]) or not os.access(config["program"], os.X_OK):
        raise ValueError("Configured program is not executable")
    if not os.path.isdir(config["working_directory"]):
        raise ValueError("Configured working directory does not exist")
    return config


def start(config):
    os.umask(0o077)
    # Remain in the SMF-created process contract. Do not create a new contract,
    # detach to another service, or use a global PID/name based stop command.
    child = subprocess.Popen([config["program"], *config["arguments"]],
                             cwd=config["working_directory"], stdin=subprocess.DEVNULL,
                             close_fds=True)
    # Preserve stdout/stderr for SMF's service log. Catch immediate exec/bind
    # failures; continued lifetime/recovery belongs to svc.startd's contract.
    time.sleep(1)
    result = child.poll()
    if result is not None:
        raise RuntimeError("BBS exited during service startup (status %d)" % result)
    return child


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("start", "check"))
    parser.add_argument("config")
    args = parser.parse_args()
    try:
        config = settings(args.config)
        if args.action == "start":
            if sys.platform != "sunos5" or not os.environ.get("SMF_FMRI", "").startswith("svc:/"):
                raise ValueError("start requires a Solaris SMF method context")
            start(config)
        else:
            print("SMF launch configuration OK (read-only check)")
    except (OSError, ValueError, RuntimeError) as error:
        # Never echo configuration/arguments: they can contain private paths.
        print("Liner Notes SMF: " + type(error).__name__ + ": " + str(error), file=sys.stderr)
        return 95  # SMF_EXIT_ERR_FATAL: administrator must fix the configuration.
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
