"""CI-only entry: open diagnostic stderr after the isolation domain transition."""
import argparse
import os
from pathlib import Path
import stat
import sys


def redirect_stderr(path):
    # Never create, truncate, follow links, or accept a public/foreign log.
    fd = os.open(path, os.O_WRONLY | os.O_APPEND | os.O_NOFOLLOW)
    try:
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_nlink != 1 or info.st_mode & 0o077:
            raise ValueError('unsafe diagnostic file')
        os.dup2(fd, 2)
    finally:
        if fd != 2: os.close(fd)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--diagnostic', required=True)
    parser.add_argument('arguments', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.arguments or args.arguments[0] != '--':
        parser.error('expected fixed isolation arguments after --')
    redirect_stderr(args.diagnostic)
    # Both files are in the existing root-owned fixture scripts directory.
    # Keep the shipped Python isolation flags, PID, environment and argv.
    target = Path(__file__).with_name('door_isolation.py')
    os.execv(sys.executable, [sys.executable, '-E', '-s', '-B', str(target), *args.arguments[1:]])


if __name__ == '__main__':
    main()
