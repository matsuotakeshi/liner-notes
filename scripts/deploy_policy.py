"""Closed deployment policy shared by the controller and privileged host helper.

No network or process execution occurs at import time. A release signature is
not a deployment approval: a second, short-lived run-bound authorization is
required for every apply/resume request.
"""
import base64
import hashlib
import json
import re
import time
from pathlib import Path

TARGETS = ('rocky.ysni.org', 'debian.ysni.org', 'srv.ysni.org')
PLATFORMS = dict(zip(TARGETS, ('rocky10', 'debian13', 'freebsd15')))
PACKAGES = {'liner-notes', 'liner-notes-web', 'liner-notes-link', 'liner-notes-selinux'}
SERVICES = {
    'freebsd15': ('liner_notes', 'liner_notes_web', 'liner_notes_link'),
    'rocky10': ('liner-notes.service', 'liner-notes-web.service', 'liner-notes-link.service'),
    'debian13': ('liner-notes.service', 'liner-notes-web.service', 'liner-notes-link.service'),
}
HEX40 = re.compile(r'[0-9a-f]{40}\Z')
HEX64 = re.compile(r'[0-9a-f]{64}\Z')
SAFE_NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9._+~-]{0,180}\Z')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical(document):
    return json.dumps(document, sort_keys=True, separators=(',', ':'), ensure_ascii=True).encode('ascii')


def digest_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(data)
    return digest.hexdigest()


def sign(document, private_pem):
    from cryptography.hazmat.primitives.serialization import load_pem_private_key
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    key = load_pem_private_key(private_pem, password=None)
    require(isinstance(key, Ed25519PrivateKey), 'An Ed25519 signing key is required')
    return {'document': document, 'signature': base64.b64encode(key.sign(canonical(document))).decode('ascii')}


def verify(envelope, public_pem):
    from cryptography.hazmat.primitives.serialization import load_pem_public_key
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
    require(isinstance(envelope, dict) and set(envelope) == {'document', 'signature'}, 'Invalid signed envelope')
    key = load_pem_public_key(public_pem)
    require(isinstance(key, Ed25519PublicKey), 'An Ed25519 verification key is required')
    try:
        key.verify(base64.b64decode(envelope['signature'], validate=True), canonical(envelope['document']))
    except Exception as exc:
        raise ValueError('Signature verification failed') from exc
    return envelope['document']


def validate_release(doc):
    require(isinstance(doc, dict) and set(doc) == {'schema', 'version', 'source_sha', 'release_id', 'tag', 'platforms', 'rollback_compatible'}, 'Invalid deployment manifest fields')
    require(doc['schema'] == 1 and HEX40.fullmatch(doc['source_sha']), 'Invalid release source SHA')
    require(SAFE_NAME.fullmatch(doc['version']) and doc['tag'] == 'v' + doc['version'], 'Invalid version/tag')
    require(type(doc['release_id']) is int and doc['release_id'] > 0, 'Invalid release identity')
    require(type(doc['rollback_compatible']) is bool, 'Rollback compatibility must be explicit')
    require(set(doc['platforms']) == set(PLATFORMS.values()), 'Complete three-platform release required')
    for platform, packages in doc['platforms'].items():
        require(isinstance(packages, dict) and 'liner-notes' in packages and set(packages) <= PACKAGES, 'Unexpected package set')
        if platform == 'freebsd15':
            require(set(packages) == {'liner-notes'}, 'FreeBSD uses the single comms/liner-notes package')
        else:
            require({'liner-notes', 'liner-notes-web', 'liner-notes-link'} <= set(packages), 'Linux requires the complete core/web/link release set')
            require(platform == 'rocky10' or 'liner-notes-selinux' not in packages, 'SELinux RPM is Rocky-only')
        for name, item in packages.items():
            require(set(item) == {'filename', 'sha256', 'version', 'size'}, 'Invalid package metadata')
            require(SAFE_NAME.fullmatch(item['filename']) and HEX64.fullmatch(item['sha256']), 'Unsafe package filename/hash')
            require(type(item['size']) is int and 0 < item['size'] <= 4 * 1024**3, 'Invalid package size')
            require(isinstance(item['version'], str) and SAFE_NAME.fullmatch(item['version']), 'Invalid package version')
            require(item['version'] == doc['version'] or item['version'].startswith(doc['version'] + '-'), 'Package version differs from release')
    return doc


def approval_phrase(doc, run_id, attempt):
    return 'DEPLOY ' + doc['tag'] + ' ' + doc['source_sha'] + ' ' + ','.join(TARGETS) + f' RUN {run_id}/{attempt}'


def authorization(doc, run_id, attempt, actor_id, now=None):
    now = int(time.time() if now is None else now)
    require(all(type(x) is int and x > 0 for x in (run_id, attempt, actor_id)), 'Invalid approval identity')
    return dict(schema=1, manifest_sha256=hashlib.sha256(canonical(doc)).hexdigest(),
                source_sha=doc['source_sha'], release_id=doc['release_id'], targets=list(TARGETS),
                run_id=run_id, run_attempt=attempt, actor_id=actor_id,
                issued_at=now, expires_at=now + 3600)


def validate_authorization(auth, doc, target, now=None):
    now = int(time.time() if now is None else now)
    expected = authorization(doc, auth.get('run_id'), auth.get('run_attempt'), auth.get('actor_id'), auth.get('issued_at'))
    require(auth == expected and auth['targets'] == list(TARGETS), 'Authorization does not match this release/run/target set')
    require(target in TARGETS and auth['issued_at'] <= now < auth['expires_at'], 'Expired or future deployment authorization')
    return auth


def transaction_id(doc):
    return str(doc['release_id']) + '-' + doc['source_sha']


def validate_environment(environment, branches, approvals, doc, run_id, attempt):
    """Arguments must be fresh authenticated GitHub API responses, not inputs."""
    require(environment.get('name') == 'production-release', 'Wrong protected environment')
    rules = environment.get('protection_rules', [])
    reviewers = [r for r in rules if r.get('type') == 'required_reviewers']
    require(len(reviewers) == 1, 'Environment required-reviewer protection is not enabled')
    # This repository's owner is the explicitly authorized operator. Team
    # membership requires a separately audited implementation; do not infer it.
    allowed = {r['reviewer']['id'] for r in reviewers[0].get('reviewers', []) if r.get('type') == 'User'}
    require(34602508 in allowed, 'Owner approval is not enforced by the environment')
    policy = environment.get('deployment_branch_policy') or {}
    require(policy.get('custom_branch_policies') is True, 'Explicit main-only environment policy required')
    require(branches.get('branch_policies') and all(r.get('name') == 'main' and r.get('type', 'branch') == 'branch' for r in branches['branch_policies']), 'Environment permits a non-main ref')
    phrase = approval_phrase(doc, run_id, attempt)
    matched = [r for r in approvals if any(e.get('id') == environment['id'] for e in r.get('environments', []))]
    require(matched and not any(r.get('state') == 'rejected' for r in matched), 'Current run lacks an approved review')
    accepted = [r for r in matched if r.get('state') == 'approved' and r.get('user', {}).get('id') == 34602508 and r.get('comment') == phrase]
    require(accepted, 'Approval comment must bind the release, SHA, targets and current run attempt')
    return 34602508


def package_commands(platform, packages, directory, rollback=False):
    """No generic OS update, deletion or dependency resolution is permitted."""
    require(platform in PLATFORMS.values() and set(packages) <= PACKAGES and 'liner-notes' in packages, 'Unexpected package target')
    files = [str(Path(directory) / packages[n]['filename']) for n in sorted(packages)]
    if platform == 'rocky10':
        return [['/usr/bin/rpm', '-U', '--oldpackage', *files]] if rollback else [['/usr/bin/rpm', '-U', *files]]
    if platform == 'debian13':
        return [['/usr/bin/dpkg', '--force-confold', '--install', *files]]
    require(set(packages) == {'liner-notes'}, 'Only liner-notes may be upgraded on FreeBSD')
    # A private, single-package file:// repository gives pkg an actual upgrade
    # transaction. No global catalog, pkg delete or pkg add -f is used.
    return [['/usr/local/sbin/pkg', '-o', 'REPOS_DIR=' + str(Path(directory)/'repos'),
             '-o', 'PKG_CACHEDIR=' + str(Path(directory)/'cache'),
             'upgrade', '-y', '-U', '-r', 'liner_notes_release', 'liner-notes']]


def service_command(platform, service, action):
    require(service in SERVICES[platform] and action in {'start', 'stop', 'status'}, 'Forbidden service operation')
    if platform == 'freebsd15':
        return ['/usr/sbin/service', service, {'status':'onestatus'}.get(action, action)]
    return ['/usr/bin/systemctl', {'status':'is-active'}.get(action, action), service]
