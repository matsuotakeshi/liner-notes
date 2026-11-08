#!/usr/bin/python3
"""Inspect policy, or explicitly restore vendor/local labels on known paths."""
import argparse
import os
from pathlib import Path
import subprocess

PATHS = ('/usr/sbin/liner-notes', '/usr/sbin/liner-notes-web',
         '/usr/bin/liner-notes-link', '/usr/bin/liner-notes-admin',
         '/usr/bin/liner-notes-memberctl', '/usr/bin/liner-notes-vaultctl',
         '/usr/bin/liner-notes-dbconvert', '/usr/bin/liner-notes-zmodem',
         '/usr/share/liner-notes', '/usr/libexec/liner-notes/doors',
         '/etc/liner-notes', '/var/lib/liner-notes', '/var/lib/liner-notes-web',
         '/var/lib/liner-notes-link', '/var/lib/liner-notes-doors',
         '/var/backups/liner-notes',
         '/var/log/liner-notes', '/var/log/liner-notes-web', '/var/log/liner-notes-link',
         '/run/liner-notes', '/run/liner-notes-web', '/run/liner-notes-link')


def existing_paths():
    paths = []
    for name in PATHS:
        p = Path(name)
        if any(x.is_symlink() for x in (p, *p.parents)):
            raise ValueError('Refusing symlink path: ' + name)
        if p.exists():
            paths.append(name)
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--relabel', action='store_true', help='explicitly apply default/local labels; no service or port changes')
    args = parser.parse_args()
    if args.relabel:
        if os.geteuid() != 0:
            parser.error('--relabel requires root')
        for path in existing_paths():
            # No -F: preserve customizable labels, and honor semanage fcontext.
            subprocess.run(['/usr/sbin/restorecon', '-R', '-v', path], check=True)
    else:
        subprocess.run(['/usr/sbin/getenforce'], check=True)
        result = subprocess.check_output(['/usr/sbin/semodule', '-lfull'], text=True)
        for line in result.splitlines():
            if 'liner_notes' in line.split():
                print(line)


if __name__ == '__main__':
    main()
