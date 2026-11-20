"""CI-only entry: open diagnostic stderr after the isolation domain transition."""
import argparse
import errno
import json
import os
from pathlib import Path
import runpy
import stat
import sys


def permission_trace():
    """Only two known frames, numeric source lines/errno, never traceback/locals."""
    directory = Path(__file__).parent
    targets = {str(directory/'door_isolation.py'): 'slot_acquire',
               str(directory/'door_processes.py'): 'processes'}
    def trace(frame, event, arg):
        function = targets.get(frame.f_code.co_filename)
        if function is None or frame.f_code.co_name != function:
            return None
        if event == 'exception':
            error = arg[1]
            if isinstance(error, OSError) and error.errno in (errno.EACCES, errno.EPERM):
                try:
                    print(json.dumps({'event': 'slot-permission-error', 'function': function,
                                      'line': frame.f_lineno, 'errno': error.errno}),
                          file=sys.stderr, flush=True)
                except Exception:
                    pass # evidence must not alter refusal/cleanup
        return trace
    return trace


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
    # Keep Python flags, PID, environment and argv. This CI-only runpy variant
    # observes only permission exceptions from the two fixed functions above.
    target = Path(__file__).with_name('door_isolation.py')
    previous_trace = sys.gettrace()
    previous_argv = sys.argv
    try:
        sys.argv = [str(target), *args.arguments[1:]]
        sys.settrace(permission_trace())
        runpy.run_path(str(target), run_name='__main__')
    finally:
        sys.settrace(previous_trace)
        sys.argv = previous_argv


if __name__ == '__main__':
    main()
