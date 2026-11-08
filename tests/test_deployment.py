#!/usr/bin/env python3
"""No SSH or package manager access: isolated files/SQLite/transport fixtures."""
import contextlib
import copy
import hashlib
import io
import json
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'ci')]
import deploy_policy as policy
from deploy_transaction import apply_host, atomic_json, rollout
from deploy_backup_store import receive
from deploy_restore import health_probes
from deploy_release import successful_execution, latest_full_run, trusted_release_ancestor, main as controller_main, record_target
from deploy_host import Host, validate_backup_inventory
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PrivateFormat, PublicFormat, NoEncryption


def release():
    packages = lambda extension: {n: {'filename': n + '-0.75-1.' + extension, 'sha256': 'a' * 64, 'size': 1, 'version': '0.75-1'}
                                  for n in ('liner-notes', 'liner-notes-web', 'liner-notes-link')}
    return {'schema': 1, 'source_sha': 'b' * 40, 'version': '0.75', 'tag': 'v0.75', 'release_id': 123,
            'rollback_compatible': False, 'platforms': {'freebsd15': {'liner-notes': packages('pkg')['liner-notes']},
                                                      'rocky10': packages('rpm'), 'debian13': packages('deb')}}


class Fixture:
    """Durable subprocess fixture; the real OS package adapters have separate CI.

    Exercises state recovery and failure ordering without claiming production
    services or native package scripts ran inside this hosted unit test.
    """
    def __init__(self, path, target, failure=None):
        self.path, self.target, self.failure = path, target, failure
        path.mkdir()
        (path / 'version').write_text('old')
        (path / 'service').write_text('running')
        self.calls = []
        with contextlib.closing(sqlite3.connect(path / 'data.sqlite')) as db:
            db.execute('CREATE TABLE articles (body TEXT)')
            db.execute("INSERT INTO articles VALUES ('retained article')")
            db.commit()

    def operation(self, name, filename=None, content=None):
        self.calls.append(name)
        if self.failure == name:
            raise ValueError('injected failure containing SECRET')
        if filename:
            subprocess.run([sys.executable, '-c', 'from pathlib import Path; import sys; Path(sys.argv[1]).write_text(sys.argv[2])',
                            str(self.path / filename), content], check=True, capture_output=True)

    def lock(self):
        return contextlib.nullcontext()

    def load_state(self, identity):
        path = self.path / 'state.json'
        return json.loads(path.read_text()) if path.exists() else None

    def save_state(self, identity, state):
        atomic_json(self.path / 'state.json', state)

    def preflight(self, doc):
        self.operation('preflight')
        return {'liner-notes': (self.path / 'version').read_text()}

    def stop(self):
        self.operation('stop', 'service', 'stopped')

    def start(self):
        self.operation('start', 'service', 'running')

    def backup(self, identity):
        self.operation('backup')
        # Actual independent SQLite copy + restore and digest, not a boolean
        # passed by the test as the backup result.
        with contextlib.closing(sqlite3.connect(self.path / 'data.sqlite')) as source:
            with contextlib.closing(sqlite3.connect(self.path / 'restored.sqlite')) as destination:
                source.backup(destination)
                assert destination.execute('SELECT body FROM articles').fetchall() == [('retained article',)]
        data = (self.path / 'restored.sqlite').read_bytes()
        sink = self.path / 'offhost'
        sink.mkdir(mode=0o700)
        meta = {'id': self.target + '-' + identity, 'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data)}
        receive(meta, io.BytesIO(data), sink)
        return dict(meta, restored=True, off_host_verified=True)

    def install(self, doc):
        self.operation('install', 'version', doc['version'])

    def health(self, doc):
        self.operation('health-after' if doc else 'health-before')
        assert (self.path / 'service').read_text() == 'running'
        with contextlib.closing(sqlite3.connect(self.path / 'data.sqlite')) as db:
            assert db.execute('SELECT body FROM articles').fetchall() == [('retained article',)]
        return {'liner-notes': (self.path / 'version').read_text()}


class Deployment(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.doc = release()
        self.auth = policy.authorization(self.doc, 100, 2, 34602508)

    def fixtures(self, failure=None):
        return {target: Fixture(self.base / str(i), target, failure if i == 1 else None) for i, target in enumerate(policy.TARGETS)}

    def test_complete_three_node_sequence_and_repeated_apply(self):
        hosts = self.fixtures()
        order = []
        def apply(target, doc, auth):
            order.append(target)
            return apply_host(doc, auth, target, hosts[target])
        result = rollout(self.doc, self.auth, apply, dry_run=False)
        self.assertEqual(order, list(policy.TARGETS))
        self.assertTrue(all(r['result'] == 'healthy' for r in result))
        repeated = rollout(self.doc, policy.authorization(self.doc, 101, 1, 34602508), apply, dry_run=False)
        self.assertTrue(all(h.calls.count('install') == 1 for h in hosts.values()))
        self.assertTrue(all(r['approval']['run_id'] == 101 and r['original_approval']['run_id'] == 100 for r in repeated))

    def test_first_transport_failure_is_persisted_without_contacting_later_hosts(self):
        results, contacted = [], []
        output = self.base / 'audit.json'
        def invoke():
            raise RuntimeError('SECRET transport credential text')
        def apply(target, doc, approval):
            contacted.append(target)
            return record_target(target, doc, invoke, results, lambda: output.write_text(json.dumps(results)))
        with self.assertRaises(ValueError):
            rollout(self.doc, self.auth, apply, dry_run=False)
        self.assertEqual(contacted, [policy.TARGETS[0]])
        self.assertEqual(json.loads(output.read_text())[0]['result'], 'transport-or-admission-failed')
        self.assertNotIn('SECRET', output.read_text())

    def test_admission_failure_still_emits_redacted_identity_audit(self):
        output = self.base / 'audit.json'
        with patch.dict(os.environ, {'GITHUB_REPOSITORY': 'untrusted/repo', 'GITHUB_SHA': self.doc['source_sha'],
                                     'GITHUB_RUN_ID': '100', 'GITHUB_RUN_ATTEMPT': '2'}), \
             patch.object(sys, 'argv', ['deploy_release.py', '--release-id', '123', '--output', str(output)]), \
             self.assertRaises(ValueError):
            controller_main()
        result = json.loads(output.read_text())
        self.assertEqual(result['controller_sha'], self.doc['source_sha'])
        self.assertNotIn('source_sha', result)  # unverified release identity is unknown at this point
        self.assertEqual(result['result'], 'rejected-or-stopped')
        self.assertEqual(result['run_attempt'], '2')

    def test_default_plan_never_calls_transport(self):
        result = rollout(self.doc, self.auth, lambda *args: self.fail('SSH in dry run'))
        self.assertEqual(len(result), 3)

    def test_midway_failure_stops_later_hosts_and_requires_planned_restore(self):
        hosts = self.fixtures('health-after')
        def apply(target, doc, auth):
            return apply_host(doc, auth, target, hosts[target])
        with self.assertRaises(ValueError):
            rollout(self.doc, self.auth, apply, dry_run=False)
        self.assertEqual(hosts[policy.TARGETS[2]].calls, [])
        failed = hosts[policy.TARGETS[1]]
        self.assertEqual(failed.load_state('')['result'], 'planned-restore-required')
        self.assertNotIn('SECRET', (failed.path / 'state.json').read_text())
        self.assertEqual((failed.path / 'service').read_text(), 'stopped')
        with self.assertRaises(ValueError):
            apply_host(self.doc, self.auth, policy.TARGETS[1], failed)

    def test_backup_failure_prevents_install(self):
        host = Fixture(self.base / 'host', policy.TARGETS[0], 'backup')
        with self.assertRaises(ValueError):
            apply_host(self.doc, self.auth, policy.TARGETS[0], host)
        self.assertNotIn('install', host.calls)

    def test_partly_stopped_service_is_never_reported_as_preflight_failure(self):
        host = Fixture(self.base / 'host', policy.TARGETS[0], 'stop')
        with self.assertRaises(ValueError):
            apply_host(self.doc, self.auth, policy.TARGETS[0], host)
        self.assertEqual(host.load_state('')['result'], 'planned-restore-required')
        self.assertNotIn('install', host.calls)

    def test_preflight_failure_does_not_stop_service(self):
        host = Fixture(self.base / 'host', policy.TARGETS[0], 'preflight')
        with self.assertRaises(ValueError):
            apply_host(self.doc, self.auth, policy.TARGETS[0], host)
        self.assertNotIn('stop', host.calls)

    def test_interrupted_state_cannot_be_reapplied(self):
        host = Fixture(self.base / 'host', policy.TARGETS[0])
        host.save_state('', {'manifest_sha256': hashlib.sha256(policy.canonical(self.doc)).hexdigest(), 'result': 'installed'})
        with self.assertRaises(ValueError):
            apply_host(self.doc, self.auth, policy.TARGETS[0], host)
        self.assertEqual(host.calls, [])

    @unittest.skipUnless(os.name == 'posix', 'POSIX host flock is exercised on hosted Linux')
    def test_failed_host_transaction_blocks_a_different_release(self):
        host = object.__new__(Host)
        host.root = self.base
        atomic_json(self.base / 'previous-release.json', {'result': 'planned-restore-required'})
        with self.assertRaises(ValueError):
            with host.lock():
                self.fail('A different release bypassed unresolved recovery')

    def test_approval_binding_rejects_tampering_expiry_and_new_sha(self):
        for field, value in [('source_sha', 'c' * 40), ('release_id', 124), ('targets', list(reversed(policy.TARGETS))), ('manifest_sha256', 'd' * 64)]:
            changed = dict(self.auth, **{field: value})
            with self.subTest(field=field), self.assertRaises(ValueError):
                policy.validate_authorization(changed, self.doc, policy.TARGETS[0])
        with self.assertRaises(ValueError):
            policy.validate_authorization(self.auth, self.doc, policy.TARGETS[0], now=self.auth['expires_at'])
        with self.assertRaises(ValueError):
            policy.validate_authorization(self.auth, dict(self.doc, source_sha='f' * 40), policy.TARGETS[0])

    def test_signatures_reject_changed_artifact_or_another_key(self):
        key = Ed25519PrivateKey.generate()
        private = key.private_bytes(Encoding.PEM, PrivateFormat.PKCS8, NoEncryption())
        public = key.public_key().public_bytes(Encoding.PEM, PublicFormat.SubjectPublicKeyInfo)
        envelope = policy.sign(self.doc, private)
        self.assertEqual(policy.verify(envelope, public), self.doc)
        envelope['document']['platforms']['rocky10']['liner-notes']['sha256'] = 'e' * 64
        with self.assertRaises(ValueError):
            policy.verify(envelope, public)

    def test_environment_review_requires_exact_current_attempt(self):
        env = {'id': 7, 'name': 'production-release', 'protection_rules': [{'type': 'required_reviewers', 'reviewers': [{'type': 'User', 'reviewer': {'id': 34602508}}]}], 'deployment_branch_policy': {'custom_branch_policies': True}}
        branches = {'branch_policies': [{'name': 'main', 'type': 'branch'}]}
        approval = {'state': 'approved', 'environments': [{'id': 7}], 'user': {'id': 34602508}, 'comment': policy.approval_phrase(self.doc, 100, 2)}
        self.assertEqual(policy.validate_environment(env, branches, [approval], self.doc, 100, 2), 34602508)
        for reviews, attempt in [([], 2), ([approval], 3), ([dict(approval, comment='approved')], 2), ([dict(approval, user={'id': 1})], 2)]:
            with self.assertRaises(ValueError):
                policy.validate_environment(env, branches, reviews, self.doc, 100, attempt)
        with self.assertRaises(ValueError):
            policy.validate_environment(env, {'branch_policies': [{'name': '*'}]}, [approval], self.doc, 100, 2)

    def test_pem_reformatting_is_not_a_separate_approval_key(self):
        key = Ed25519PrivateKey.generate().public_key().public_bytes(Encoding.PEM, PublicFormat.SubjectPublicKeyInfo)
        self.assertEqual(policy.public_key_identity(key), policy.public_key_identity(key.replace(b'\n', b'\r\n')))
        other = Ed25519PrivateKey.generate().public_key().public_bytes(Encoding.PEM, PublicFormat.SubjectPublicKeyInfo)
        self.assertNotEqual(policy.public_key_identity(key), policy.public_key_identity(other))

    def test_skipped_successful_workflow_does_not_count_as_test(self):
        jobs = {'total_count': 1, 'jobs': [{'conclusion': 'success', 'steps': [{'name': 'actual test', 'conclusion': 'skipped'}]}]}
        with self.assertRaises(ValueError):
            successful_execution(jobs, 'actual test')
        jobs['jobs'][0]['steps'][0]['conclusion'] = 'success'
        successful_execution(jobs, 'actual test')
        with self.assertRaises(ValueError):
            successful_execution(jobs, 'actual test', 2)

    def test_manual_full_run_allowed_but_new_failed_attempt_cannot_be_hidden(self):
        base = {'id': 1, 'run_number': 1, 'run_attempt': 1, 'head_branch': 'main',
                'head_sha': self.doc['source_sha'], 'status': 'completed', 'conclusion': 'success', 'event': 'push'}
        manual = dict(base, id=2, run_number=2, event='workflow_dispatch')
        self.assertEqual(latest_full_run({'workflow_runs': [base, manual]}, self.doc['source_sha'], 'push', 'test'), 2)
        failed = dict(manual, run_attempt=2, conclusion='failure')
        with self.assertRaises(ValueError):
            latest_full_run({'workflow_runs': [base, failed]}, self.doc['source_sha'], 'push', 'test')
        with self.assertRaises(ValueError):
            latest_full_run({'workflow_runs': [dict(manual, head_branch='feature')]}, self.doc['source_sha'], 'push', 'test')

    def test_later_main_controller_requires_release_ancestry(self):
        release_sha, controller_sha = 'a' * 40, 'b' * 40
        comparison = {'status': 'ahead', 'base_commit': {'sha': release_sha},
                      'merge_base_commit': {'sha': release_sha}}
        with patch('deploy_release.api', side_effect=[{'object': {'sha': controller_sha}}, comparison]):
            trusted_release_ancestor(release_sha, controller_sha)
        for changed in (dict(comparison, status='diverged'),
                        dict(comparison, merge_base_commit={'sha': 'c' * 40})):
            with patch('deploy_release.api', side_effect=[{'object': {'sha': controller_sha}}, changed]):
                with self.assertRaises(ValueError):
                    trusted_release_ancestor(release_sha, controller_sha)
        with patch('deploy_release.api', return_value={'object': {'sha': 'c' * 40}}):
            with self.assertRaises(ValueError):
                trusted_release_ancestor(release_sha, controller_sha)

    def test_package_and_service_commands_are_closed(self):
        for platform, packages in self.doc['platforms'].items():
            command = policy.package_commands(platform, packages, self.base)[0]
            self.assertNotIn('delete', command)
            self.assertNotIn('remove', command)
            if platform == 'freebsd15':
                self.assertEqual(command[-1], 'liner-notes')
                self.assertIn('liner_notes_release', command)
            for forbidden in ('apache24', 'SofChat', 'reboot', 'httpd.service'):
                with self.assertRaises(ValueError):
                    policy.service_command(platform, forbidden, 'stop')
            with self.assertRaises(ValueError):
                policy.package_commands(platform, {'apache24': {'filename': 'apache.pkg'}}, self.base)

    def test_backup_inventory_requires_explicit_feature_coverage(self):
        profile = json.loads((ROOT/'ci/deploy-host.example.json').read_text())
        validate_backup_inventory(profile, 'freebsd15')
        for feature in ('binaries', 'web', 'link', 'vault'):
            broken = copy.deepcopy(profile)
            broken['backup_roots'][feature] = []
            with self.assertRaises(ValueError):
                validate_backup_inventory(broken, 'freebsd15')
        broken = copy.deepcopy(profile)
        broken['backup_features']['web'] = False
        broken['backup_roots']['web'] = []
        with self.assertRaises(ValueError):
            validate_backup_inventory(broken, 'freebsd15')
        profile['services'] = ['liner_notes']
        profile['backup_features']['web'] = False
        profile['backup_roots']['web'] = []
        validate_backup_inventory(profile, 'freebsd15')

    def test_installed_inventory_preserves_multiple_kernel_versions(self):
        host = object.__new__(Host)
        host.platform = 'rocky10'
        output = 'kernel-core\t6.12.1\tx86_64\nkernel-core\t6.12.2\tx86_64\nliner-notes\t0.75-1\tx86_64'
        with patch('deploy_host.command', return_value=output):
            inventory = host.inventory()
        self.assertEqual(len(inventory), 3)
        self.assertEqual(inventory['liner-notes'], ['0.75-1', 'x86_64'])

    def test_package_policy_requires_complete_set_and_provenance_version(self):
        policy.validate_release(self.doc)
        changed = copy.deepcopy(self.doc)
        del changed['platforms']['debian13']['liner-notes-web']
        with self.assertRaises(ValueError):
            policy.validate_release(changed)
        optional = copy.deepcopy(self.doc)
        optional['platforms']['rocky10']['liner-notes-selinux'] = dict(
            optional['platforms']['rocky10']['liner-notes'], filename='liner-notes-selinux-0.1.0-1.el10.noarch.rpm', version='0.1.0-1.el10')
        policy.validate_release(optional)
        changed = copy.deepcopy(self.doc)
        changed['platforms']['freebsd15']['liner-notes']['version'] = '0.74.1'
        with self.assertRaises(ValueError):
            policy.validate_release(changed)

    @unittest.skipUnless(os.name == 'posix', 'POSIX O_NOFOLLOW package-copy guard is exercised on hosted Linux')
    def test_real_adapter_rejects_package_hash_before_any_package_command(self):
        host = object.__new__(Host)
        host.root = self.base / 'state'
        host.root.mkdir()
        host.platform = 'rocky10'
        incoming = self.base / 'incoming'
        folder = incoming / policy.transaction_id(self.doc)
        folder.mkdir(parents=True)
        (folder / self.doc['platforms']['rocky10']['liner-notes']['filename']).write_bytes(b'x')
        host.profile = {'incoming_directory': str(incoming)}
        host.inventory = lambda: {'liner-notes': ['0.74.1-1', 'x86_64']}
        with patch('deploy_host.command', side_effect=AssertionError('package command ran before hash verification')):
            with self.assertRaisesRegex(ValueError, 'integrity mismatch'):
                host.preflight(self.doc)

    def test_offhost_sink_rejects_corruption_and_accepts_exact_retry(self):
        root = self.base / 'sink'
        root.mkdir(mode=0o700)
        payload = b'isolated backup data'
        meta = {'id': policy.TARGETS[0] + '-' + policy.transaction_id(self.doc), 'sha256': hashlib.sha256(payload).hexdigest(), 'size': len(payload)}
        receive(meta, io.BytesIO(payload), root)
        receive(meta, io.BytesIO(payload), root)
        with self.assertRaises(ValueError):
            receive(meta, io.BytesIO(b'x' * len(payload)), root)
        self.assertEqual((root / (meta['id'] + '.tar')).read_bytes(), payload)

    def test_local_tcp_health_probe_executes(self):
        with socket.socket() as server:
            server.bind(('127.0.0.1', 0))
            server.listen()
            def serve():
                connection, _ = server.accept()
                with connection:
                    self.assertEqual(connection.recv(1024), b'READ 1\n')
                    connection.sendall(b'retained article\nNEXT PROMPT\n')
            thread = threading.Thread(target=serve)
            thread.start()
            health_probes({'article': {'host': '127.0.0.1', 'port': server.getsockname()[1], 'exchanges': [{'send': 'READ 1\n', 'expect': 'retained article'}, {'expect': 'NEXT PROMPT'}]}})
            thread.join(5)
            self.assertFalse(thread.is_alive())


if __name__ == '__main__':
    unittest.main()
