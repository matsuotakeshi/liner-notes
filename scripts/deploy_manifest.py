#!/usr/bin/env python3
"""Sign an already verified CI release set; never publish/deploy it."""
import argparse
import json
from pathlib import Path
from deploy_policy import sign, validate_release
from verify_release_set import verify_set


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--candidates', type=Path, required=True)
    p.add_argument('--source-sha', required=True)
    p.add_argument('--release-id', type=int, required=True)
    p.add_argument('--private-key', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    receipt = verify_set(a.candidates, a.source_sha)
    platforms = {}
    for platform in receipt['platforms']:
        manifest = json.loads((a.candidates / platform / 'RELEASE-MANIFEST.json').read_text())
        platforms[platform] = {name: dict(item, version=manifest['version']) for name, item in manifest['packages'].items()}
    document = validate_release(dict(schema=1, version=receipt['version'], tag='v' + receipt['version'],
                                     source_sha=a.source_sha, release_id=a.release_id,
                                     platforms=platforms, rollback_compatible=False))
    signed = sign(document, a.private_key.read_bytes())
    with a.output.open('x', encoding='utf-8') as stream:
        json.dump(signed, stream, indent=2, sort_keys=True)
        stream.write('\n')


if __name__ == '__main__':
    main()
