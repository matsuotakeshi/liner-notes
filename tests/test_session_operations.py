"""Separate-process restart regression using one isolated real store."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def run_phase(binary, directory, phase, ids=()):
    command = [str(binary), '--session-' + phase, str(directory), *map(str, ids)]
    try:
        result = subprocess.run(command, check=False, capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired as error:
        print(f'SESSION-API harness timeout phase={phase} (120 seconds)', file=sys.stderr, flush=True)
        for output in (error.stdout, error.stderr):
            if output:
                print(output.decode(errors='replace') if isinstance(output, bytes) else output,
                      file=sys.stderr, flush=True)
        raise
    # C diagnostics include only phase/operation/result IDs, never request payloads.
    if result.stdout:
        print(result.stdout, end='', flush=True)
    if result.stderr:
        print(result.stderr, end='', file=sys.stderr, flush=True)
    if result.returncode:
        raise RuntimeError(f'SESSION-API phase={phase} exit={result.returncode}')
    return result.stdout


def run_checks(binary, directory):
    seeded = run_phase(binary, directory, 'seed')
    rows = re.findall(r'^SESSION_FIXTURE ([1-9][0-9]*) ([1-9][0-9]*)$', seeded, re.M)
    if len(rows) != 1 or any(int(value) > 0xffffffff for value in rows[0]):
        raise RuntimeError('SESSION-API seed did not provide one valid article/mail identity pair')
    # run_phase waits for the seed process to exit successfully before starting
    # a fresh process. No worker globals or in-memory indexes can survive.
    run_phase(binary, directory, 'verify', tuple(map(int, rows[0])))


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='liner-session-api-') as directory:
        run_checks(Path(sys.argv[1]).resolve(), directory)
