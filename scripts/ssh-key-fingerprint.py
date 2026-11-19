#!/usr/bin/env python3
"""Print one BBS member-key allowlist row; never write settings or read private keys."""
import argparse
import base64
import hashlib
from pathlib import Path


def row(member, public):
    fields = public.strip().split()
    if not 1 <= member <= 999999 or len(fields) < 2 or fields[0] not in (
            'ssh-ed25519', 'ssh-rsa', 'ecdsa-sha2-nistp256',
            'ecdsa-sha2-nistp384', 'ecdsa-sha2-nistp521'):
        raise ValueError('Expected a numeric member ID and a plain OpenSSH public-key line (no options)')
    blob = base64.b64decode(fields[1], validate=True)
    n = int.from_bytes(blob[:4], 'big')
    if len(blob) > 16384 or len(blob) < 8 or blob[4:4+n].decode('ascii') != fields[0] or n >= len(blob)-4:
        raise ValueError('Invalid public-key encoding')
    return f'{member} {hashlib.sha256(blob).hexdigest()}'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('member', type=int)
    parser.add_argument('public_key', type=Path)
    args = parser.parse_args()
    try:
        if args.public_key.stat().st_size > 32768:
            raise ValueError('Public-key file is too large')
        print(row(args.member, args.public_key.read_text(encoding='ascii')))
    except (OSError, ValueError, UnicodeError) as error:
        parser.exit(2, f'{error}\n')
