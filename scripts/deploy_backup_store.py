#!/usr/bin/env python3
"""Restricted off-host archive receiver. Run as a dedicated unprivileged user.

Install with a forced SSH command or a wrapper permitting this program only;
its private directory is fixed, never supplied by the client.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import sys
from deploy_policy import HEX64, digest_file, require

STORE = Path('/var/lib/liner-notes-release-backups')


def receive(meta, stream, root=STORE):
    require(set(meta) == {'id', 'sha256', 'size'}, 'Invalid backup metadata')
    require(re.fullmatch(r'[1-9][0-9]*-[0-9a-f]{40}', meta['id']) and HEX64.fullmatch(meta['sha256']), 'Invalid backup identity')
    require(type(meta['size']) is int and 0 < meta['size'] <= 16 * 1024**4, 'Invalid backup size')
    require(root.is_dir() and not root.is_symlink() and not root.stat().st_mode & 0o077, 'Backup directory must be private')
    target = root / (meta['id'] + '.tar')
    if target.exists():
        require(not target.is_symlink() and target.stat().st_size == meta['size'] and digest_file(target) == meta['sha256'], 'Backup identity already has different content')
        # Consume and verify retries too; never return success for unrelated data.
    temporary = root / (meta['id'] + '.incoming')
    digest, count = hashlib.sha256(), 0
    created = False
    try:
        with temporary.open('xb') as output:
            created = True
            os.chmod(temporary, 0o600)
            while True:
                data = stream.read(1024 * 1024)
                if not data:
                    break
                count += len(data)
                require(count <= meta['size'], 'Backup exceeds declared size')
                digest.update(data)
                output.write(data)
            output.flush()
            os.fsync(output.fileno())
        require(count == meta['size'] and digest.hexdigest() == meta['sha256'], 'Backup transfer checksum mismatch')
        if target.exists():
            temporary.unlink()
        else:
            os.link(temporary, target)
            temporary.unlink()
        require(digest_file(target) == meta['sha256'], 'Stored backup checksum mismatch')
        return meta
    finally:
        if created and temporary.exists():
            temporary.unlink()


if __name__ == '__main__':
    try:
        require(len(sys.argv) == 2, 'One metadata argument required')
        os.umask(0o077)
        print(json.dumps(receive(json.loads(sys.argv[1]), sys.stdin.buffer), sort_keys=True))
    except Exception:
        print('{"result":"backup-rejected"}')
        raise SystemExit(1)
