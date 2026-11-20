"""CI-only probe: report a fixed stderr write, never requests or environment."""
import argparse
import json
import os
import signal
import time

MARKER = b'LINER_DOOR_JOURNAL_PROBE\n'


def record(path):
    result = {'bytes': 0, 'errno': 0}
    try:
        result['bytes'] = os.write(2, MARKER)
    except OSError as error:
        result['errno'] = error.errno
    # The caller creates only a fresh fixture path in root-owned state storage.
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w') as stream:
        json.dump(result, stream)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--result', required=True)
    args = parser.parse_args()
    stopping = False
    def stop(signum, frame):
        nonlocal stopping
        stopping = True
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    record(args.result)
    # Keep a MainPID for the existing domain check; the fixture stops this unit.
    until = time.monotonic() + 30
    while not stopping and time.monotonic() < until:
        time.sleep(.05)


if __name__ == '__main__':
    main()
