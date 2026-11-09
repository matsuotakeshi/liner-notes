#!/usr/bin/env python3
"""Sign an already verified CI release set; never publish/deploy it."""
import argparse
import json
from pathlib import Path
from deploy_policy import require, sign, validate_release
from verify_release_set import verify_set


def release_document(candidates, source_sha, release_id, with_selinux=False):
    receipt = verify_set(candidates, source_sha, require_selinux=with_selinux)
    platforms = {}
    for platform in receipt['platforms']:
        manifest = json.loads((candidates / platform / 'RELEASE-MANIFEST.json').read_text())
        platforms[platform] = {name: dict(item, version=manifest['version']) for name, item in manifest['packages'].items()}
    if with_selinux:
        component = receipt.get('components', {}).get('rocky10-selinux', {})
        require(component.get('source_sha') == source_sha and
                set(component.get('packages', {})) == {'liner-notes-selinux'},
                'Verified SELinux component is required')
        platforms['rocky10']['liner-notes-selinux'] = dict(
            component['packages']['liner-notes-selinux'], version=component['version'])
    return validate_release(dict(schema=1, version=receipt['version'], tag='v' + receipt['version'],
                                 source_sha=source_sha, release_id=release_id,
                                 platforms=platforms, rollback_compatible=False))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--candidates', type=Path, required=True)
    p.add_argument('--source-sha', required=True)
    p.add_argument('--release-id', type=int, required=True)
    p.add_argument('--private-key', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--with-selinux', action='store_true', help='Include the separately verified Rocky policy component')
    a = p.parse_args()
    document = release_document(a.candidates, a.source_sha, a.release_id, a.with_selinux)
    signed = sign(document, a.private_key.read_bytes())
    with a.output.open('x', encoding='utf-8') as stream:
        json.dump(signed, stream, indent=2, sort_keys=True)
        stream.write('\n')


if __name__ == '__main__':
    main()
