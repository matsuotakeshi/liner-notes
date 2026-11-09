"""Sequential, restart-safe deployment state machine.

The host adapter owns privileged operations. Neither state nor a previous green
run grants permission: admission and authorization are checked before every run.
Failed/interrupted transactions require planned recovery, never binary rollback.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path

from deploy_policy import TARGETS, canonical, require, transaction_id, validate_authorization


def atomic_json(path, document):
    path = Path(path)
    temporary = path.with_suffix('.new')
    with temporary.open('x', encoding='utf-8') as stream:
        json.dump(document, stream, sort_keys=True, indent=2)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)
    if os.name == 'posix':
        fd = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(fd)
        finally:
            os.close(fd)


def apply_host(release, authorization, target, adapter):
    validate_authorization(authorization, release, target)
    identity = transaction_id(release)
    fingerprint = hashlib.sha256(canonical(release)).hexdigest()
    # This lock is machine-wide, not release-specific. A different release must
    # not run concurrently against the same database or package manager.
    with adapter.lock():
        old = adapter.load_state(identity)
        if old:
            require(old['manifest_sha256'] == fingerprint, 'Release identity already used with different content')
            require(old['result'] == 'healthy', 'Interrupted or failed deployment: planned restore/recovery required')
            now = datetime.datetime.now(datetime.timezone.utc).isoformat()
            repeated = dict(old, repeated=True, original_approval=old['approval'],
                            approval=dict(run_id=authorization['run_id'], run_attempt=authorization['run_attempt'],
                                          actor_id=authorization['actor_id']),
                            original_finished_at=old.get('finished_at'), started_at=now, finished_at=now)
            try:
                adapter.health(release)
            except Exception:
                repeated.update(result='health-recheck-failed', reason='host-health-verification-failed',
                                finished_at=datetime.datetime.now(datetime.timezone.utc).isoformat())
                adapter.save_state(identity, repeated)
                raise
            return repeated
        state = dict(schema=1, release=release['tag'], source_sha=release['source_sha'],
                     target=target, manifest_sha256=fingerprint,
                     approval=dict(run_id=authorization['run_id'], run_attempt=authorization['run_attempt'],
                                   actor_id=authorization['actor_id']),
                     started_at=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                     result='preflight', backup_id=None)
        adapter.save_state(identity, state)
        stopped = False
        try:
            # Verification/copy, dependency/Origin check, old package escrow and
            # dry-run package plan all precede the first service stop.
            state['before'] = adapter.preflight(release)
            adapter.health(None)
            stopped = True
            state['result'] = 'stopping'
            adapter.save_state(identity, state)
            adapter.stop()
            state['result'] = 'stopped'
            adapter.save_state(identity, state)
            backup = adapter.backup(identity)
            require(backup.get('restored') is True and backup.get('off_host_verified') is True,
                    'A verified off-host backup and isolated restore are required')
            state['backup_id'] = backup['id']
            state['backup_sha256'] = backup['sha256']
            state['result'] = 'backed-up'
            adapter.save_state(identity, state)
            adapter.install(release)
            state['result'] = 'installed'
            adapter.save_state(identity, state)
            adapter.start()
            state['after'] = adapter.health(release)
            state['result'] = 'healthy'
        except Exception:
            # Exception details can contain credentials/paths from packages.
            # Preserve private host logs; export only the closed reason code.
            state['result'] = 'planned-restore-required' if stopped else 'preflight-failed'
            state['reason'] = 'host-operation-failed'
            state['finished_at'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
            if stopped:
                try:
                    adapter.stop()
                except Exception:
                    state['reason'] = 'host-operation-and-stop-failed'
            adapter.save_state(identity, state)
            raise
        state['finished_at'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        adapter.save_state(identity, state)
        return state


def rollout(release, authorization, apply, *, dry_run=True):
    """`apply` is an authenticated transport, never an authorization callback."""
    results = []
    for target in TARGETS:
        validate_authorization(authorization, release, target)
        if dry_run:
            results.append({'target': target, 'result': 'plan-only'})
            continue
        result = apply(target, release, authorization)
        require(result.get('target') == target and result.get('source_sha') == release['source_sha']
                and result.get('result') == 'healthy', 'Target failed; later targets were not contacted')
        results.append(result)
    return results
