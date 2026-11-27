#!/usr/bin/env python3
"""FreeBSD-only delivery contract: no network, real signatures, fake transport."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'ci')]
import deploy_policy as policy
import deploy_release as controller
from deploy_transaction import rollout
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PrivateFormat, PublicFormat, NoEncryption


class FreeBSDDelivery(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payload = b'CI package fixture'
        self.doc = dict(schema=1, version='0.75', tag='v0.75', source_sha='a' * 40,
                        release_id=123, rollback_compatible=False, platforms={})
        for platform, ext in [('freebsd15', 'pkg'), ('rocky10', 'rpm'), ('debian13', 'deb')]:
            names = ['liner-notes'] if platform == 'freebsd15' else ['liner-notes', 'liner-notes-web', 'liner-notes-link']
            self.doc['platforms'][platform] = {n: dict(filename=f'{n}.{ext}', version='0.75',
                size=len(self.payload), sha256=hashlib.sha256(self.payload).hexdigest()) for n in names}
        policy.validate_release(self.doc)
        key = Ed25519PrivateKey.generate()
        self.private = key.private_bytes(Encoding.PEM, PrivateFormat.PKCS8, NoEncryption())
        self.public = key.public_key().public_bytes(Encoding.PEM, PublicFormat.SubjectPublicKeyInfo)
        self.envelope = policy.sign(self.doc, self.private)
        self.environment = {'id': 7, 'name': 'production-release', 'protection_rules': [
            {'type': 'required_reviewers', 'reviewers': [{'type': 'User', 'reviewer': {'id': 34602508}}]}],
            'deployment_branch_policy': {'custom_branch_policies': True}}
        self.branches = {'branch_policies': [{'name': 'main', 'type': 'branch'}]}
        self.approval = {'state': 'approved', 'environments': [{'id': 7}], 'user': {'id': 34602508},
                         'comment': policy.approval_phrase(self.doc, 100, 2, policy.FREEBSD_TARGETS)}
        self.published = {'tag_name': 'v0.75', 'draft': False, 'prerelease': False,
                          'assets': [{'name': 'DEPLOYMENT-MANIFEST.json'}]}
        self.evidence = {'freebsd_service_upgrade_verified': True, 'freebsd_previous_version': '0.74.1'}
        self.env = {'GITHUB_REPOSITORY': controller.REPO, 'GITHUB_REF': 'refs/heads/main',
                    'GITHUB_EVENT_NAME': 'workflow_dispatch', 'GITHUB_SHA': 'b' * 40,
                    'GITHUB_RUN_ID': '100', 'GITHUB_RUN_ATTEMPT': '2',
                    'LN_RELEASE_PUBLIC_KEY': self.public.decode(), 'LN_DEPLOY_AUTH_KEY': self.private.decode(),
                    'ID_ED25519_LN_DEPLOY': 'fake-private-ssh-key'}

    def auth(self, targets=policy.FREEBSD_TARGETS):
        return policy.authorization(self.doc, 100, 2, 34602508, targets=targets)

    def test_signed_scope_cannot_be_used_on_linux_or_expanded(self):
        envelope = policy.sign(self.auth(), self.private)
        auth = policy.verify(envelope, self.public)
        policy.validate_authorization(auth, self.doc, 'srv.ysni.org')
        for host in policy.TARGETS[:2]:
            with self.assertRaises(ValueError):
                policy.validate_authorization(auth, self.doc, host)
        envelope['document']['targets'] = list(policy.TARGETS)
        with self.assertRaises(ValueError):
            policy.verify(envelope, self.public)

    def test_unknown_empty_duplicate_and_reordered_scopes_fail_before_transport(self):
        for targets in [[], ['other'], ['srv.ysni.org'] * 2, list(reversed(policy.TARGETS)), None]:
            with self.subTest(targets=targets), self.assertRaises(ValueError):
                rollout(self.doc, dict(self.auth(), targets=targets), lambda *a: self.fail('transport'), dry_run=False)

    def test_freebsd_only_rollout_and_legacy_sequence(self):
        for targets in (policy.FREEBSD_TARGETS, policy.TARGETS):
            calls = []
            def apply(target, doc, auth):
                calls.append(target)
                return {'target': target, 'source_sha': doc['source_sha'], 'result': 'healthy'}
            rollout(self.doc, self.auth(targets), apply, dry_run=False)
            self.assertEqual(calls, list(targets))

    def test_dry_run_and_expired_authorization_never_contact_host(self):
        result = rollout(self.doc, self.auth(), lambda *a: self.fail('transport'))
        self.assertEqual(result, [{'target': 'srv.ysni.org', 'result': 'plan-only'}])
        auth = policy.authorization(self.doc, 100, 2, 34602508, now=1, targets=policy.FREEBSD_TARGETS)
        with self.assertRaises(ValueError):
            rollout(self.doc, auth, lambda *a: self.fail('transport'), dry_run=False)

    def test_approval_binds_scope_and_attempt(self):
        self.assertEqual(policy.validate_environment(self.environment, self.branches, [self.approval],
            self.doc, 100, 2, targets=policy.FREEBSD_TARGETS), 34602508)
        for targets, attempt in [(policy.TARGETS, 2), (policy.FREEBSD_TARGETS, 3)]:
            with self.assertRaises(ValueError):
                policy.validate_environment(self.environment, self.branches, [self.approval], self.doc, 100, attempt, targets=targets)
        with self.assertRaises(ValueError):
            policy.validate_environment(dict(self.environment, protection_rules=[]), self.branches, [self.approval],
                self.doc, 100, 2, targets=policy.FREEBSD_TARGETS)

    def api(self, path):
        if path.endswith('/releases/123'):
            return self.published
        if path.endswith('/deployment-branch-policies'):
            return self.branches
        if path.endswith('/environments/production-release'):
            return self.environment
        if path.endswith('/approvals'):
            return [self.approval]
        self.fail('Unexpected API call: ' + path)

    def download(self, argv, **kwargs):
        self.assertEqual(argv[:3], ['gh', 'release', 'download'])
        name = argv[argv.index('--pattern') + 1]
        folder = Path(argv[argv.index('--dir') + 1])
        if name == 'DEPLOYMENT-MANIFEST.json':
            (folder / name).write_text(json.dumps(self.envelope))
        else:
            self.assertEqual(name, 'liner-notes.pkg')
            (folder / name).write_bytes(self.payload)

    def run_controller(self, mode, *, transport=None, evidence=None):
        output = self.root / 'audit.json'
        output.write_text('{}')
        args = SimpleNamespace(scope='freebsd', mode=mode, release_id=123, output=output)
        with patch.dict(os.environ, self.env), patch.object(controller, 'api', side_effect=self.api), \
             patch.object(controller.subprocess, 'run', side_effect=self.download), \
             patch.object(controller, 'fresh_evidence', side_effect=evidence or (lambda *a, **k: (self.evidence, self.published))) as checks, \
             patch.object(controller, 'transport', side_effect=transport or (lambda *a: self.fail('SSH'))) as ssh:
            controller.execute(args)
        return json.loads(output.read_text()), checks, ssh

    def test_controller_plan_has_no_connectivity_requirement_or_transport(self):
        report, checks, ssh = self.run_controller('plan')
        self.assertEqual(report['targets'], ['srv.ysni.org'])
        self.assertEqual(report['production_connectivity'], 'not-checked-plan-only')
        self.assertEqual(report['approval_comment'], self.approval['comment'])
        self.assertEqual(checks.call_args.kwargs, {'scope': 'freebsd', 'connectivity': False})
        ssh.assert_not_called()

    def test_apply_downloads_only_pkg_and_sends_single_host_signed_authorization(self):
        def transport(target, release, signed, folder, key):
            self.assertEqual(target, 'srv.ysni.org')
            auth = policy.verify(signed, self.public)
            self.assertEqual(auth['targets'], ['srv.ysni.org'])
            self.assertEqual(auth['freebsd_previous_version'], '0.74.1')
            policy.validate_authorization(auth, self.doc, target)
            self.assertEqual(sorted(p.name for p in folder.iterdir()), ['DEPLOYMENT-MANIFEST.json', 'liner-notes.pkg', 'ssh-key'])
            return {'target': target, 'source_sha': self.doc['source_sha'], 'result': 'healthy'}
        report, checks, ssh = self.run_controller('apply', transport=transport)
        self.assertEqual(checks.call_count, 2)
        self.assertTrue(checks.call_args_list[0].kwargs['connectivity'])
        self.assertEqual(ssh.call_count, 1)
        self.assertEqual(set(report['package_hashes']), {'freebsd15'})

    def test_missing_signed_manifest_denies_apply(self):
        self.published['assets'] = []
        with self.assertRaises(ValueError):
            self.run_controller('apply')

    def test_missing_upgrade_acceptance_or_wrong_review_denies_apply(self):
        self.evidence['freebsd_service_upgrade_verified'] = False
        with self.assertRaises(ValueError):
            self.run_controller('apply')
        self.evidence['freebsd_service_upgrade_verified'] = True
        self.approval['comment'] = 'approved'
        with self.assertRaises(ValueError):
            self.run_controller('apply')

    def test_changed_final_upgrade_receipt_denies_transport(self):
        values = iter([(self.evidence, self.published), (dict(self.evidence, freebsd_service_upgrade_verified=False), self.published)])
        with self.assertRaises(ValueError):
            self.run_controller('apply', evidence=lambda *a, **k: next(values))

    def test_connectivity_is_scope_specific_and_expires(self):
        now = datetime.datetime.now(datetime.timezone.utc)
        step = {'name': 'Verify production SSH access (FreeBSD only)', 'conclusion': 'success', 'completed_at': now.isoformat()}
        jobs = {'jobs': [{'conclusion': 'success', 'steps': [step]}]}
        controller.fresh_connectivity(jobs, now, step=step['name'])
        with self.assertRaises(ValueError):
            controller.fresh_connectivity(jobs, now)
        with self.assertRaises(ValueError):
            controller.fresh_connectivity(jobs, now + datetime.timedelta(hours=2), step=step['name'])

    def test_failed_transport_is_recorded_and_does_not_contact_linux(self):
        def failure(*args):
            self.assertEqual(args[0], 'srv.ysni.org')
            raise RuntimeError('SECRET failure')
        with self.assertRaises(ValueError):
            self.run_controller('apply', transport=failure)
        report = json.loads((self.root / 'audit.json').read_text())
        self.assertEqual(report['results'][0]['result'], 'transport-or-admission-failed')
        self.assertNotIn('SECRET', json.dumps(report))

    def evidence_fixture(self, *, connectivity, bad_gate=None, tag_sha=None):
        paths = []
        runs = {}
        def api(path, **kwargs):
            paths.append(path)
            if path.endswith('/releases/123'):
                return self.published
            if path.endswith('/commits/v0.75'):
                return {'sha': tag_sha or self.doc['source_sha']}
            if '/actions/workflows/' in path:
                workflow = path.split('/actions/workflows/')[1].split('/')[0]
                label, gate = next((k, v) for k, v in controller.GATES.items() if v[0] == workflow)
                sha = path.split('head_sha=')[1].split('&')[0]
                run_id = len(runs) + 1
                runs[run_id] = (label, gate, sha)
                return {'workflow_runs': [{'id': run_id, 'head_sha': sha, 'head_branch': 'main',
                    'event': gate[1], 'run_number': run_id, 'run_attempt': 1, 'status': 'completed',
                    'conclusion': 'failure' if label == bad_gate else 'success'}]}
            run_id = int(path.split('/actions/runs/')[1].split('/')[0])
            label, gate, sha = runs[run_id]
            if '/jobs?' in path:
                step = gate[2]
                if label == 'connectivity':
                    step = 'Verify production SSH access (FreeBSD only)'
                steps = [{'name': step, 'conclusion': 'success', 'completed_at': datetime.datetime.now(datetime.timezone.utc).isoformat()}]
                if label == 'deployment':
                    steps.append({'name': 'Restore and boot the actual BBS using a disposable unprivileged account', 'conclusion': 'success'})
                if label == 'selinux':
                    steps.extend({'name': n, 'conclusion': 'success'} for n in [
                        'Compile the policy and its optional RPM in Rocky 10', 'Write exact-SHA Enforcing acceptance receipt'])
                jobs = [{'conclusion': 'success', 'steps': steps}] * (2 if label == 'linux' else 1)
                return {'total_count': len(jobs), 'jobs': jobs}
            return {'total_count': 1, 'artifacts': [{'id': run_id, 'name': gate[3], 'expired': False,
                'size_in_bytes': 1, 'workflow_run': {'id': run_id, 'head_sha': sha, 'head_branch': 'main'}}]}
        def artifact(artifact, basename, **kwargs):
            label = runs[artifact['id']][0]
            sha = self.doc['source_sha']
            if label in ('native', 'solaris'):
                targets = ['solaris'] if label == 'solaris' else ['freebsd', 'rocky', 'debian']
                return {'ok': True, 'source_sha': sha, 'results': [dict(target=t, source_sha=sha, status='passed') for t in targets]}
            if label == 'selinux':
                return {'source_sha': sha, 'enforcing': True, 'acceptance': 'success'}
            if label == 'release_set':
                return {'source_sha': sha, 'version': '0.75', 'platforms': {p: {'version': '0.75', 'packages': {
                    n: {k: i[k] for k in ('filename', 'sha256')} for n, i in packages.items()}} for p, packages in self.doc['platforms'].items()}}
            if label == 'freebsd_upgrade':
                return dict(source_sha=sha, candidate_version='0.75', previous_version='0.74.1',
                    candidate_package_sha256=self.doc['platforms']['freebsd15']['liner-notes']['sha256'],
                    revision_only_fixture=False, full_service_update_verified=True, backup_restore_verified=True,
                    config_data_retained=True, unrelated_services_untouched=True, old_package_restoration_verified=True)
            platform = {'freebsd': 'freebsd15', 'rocky': 'rocky10', 'debian': 'debian13'}[label]
            return dict(source_sha=sha, platform=platform, version='0.75', packages={
                n: {k: v for k, v in i.items() if k != 'version'} for n, i in self.doc['platforms'][platform].items()})
        with patch.dict(os.environ, self.env), patch.object(controller, 'api', side_effect=api), \
             patch.object(controller, 'artifact_json', side_effect=artifact), patch.object(controller, 'trusted_release_ancestor'):
            result = controller.fresh_evidence(self.doc, scope='freebsd', connectivity=connectivity)
        return result, paths

    def test_real_evidence_plan_omits_only_connectivity_and_checks_controller(self):
        (evidence, _), paths = self.evidence_fixture(connectivity=False)
        self.assertTrue(evidence['freebsd_service_upgrade_verified'])
        self.assertIn('controller_deployment', evidence)
        self.assertNotIn('connectivity', evidence)
        self.assertFalse(any('production-ssh-smoke' in p for p in paths))
        self.assertEqual(len(evidence), len(controller.GATES) + 2)

    def test_real_evidence_apply_requires_freebsd_connectivity(self):
        (evidence, _), paths = self.evidence_fixture(connectivity=True)
        self.assertIn('connectivity', evidence)
        self.assertTrue(any('production-ssh-smoke' in p for p in paths))

    def test_real_evidence_failed_ci_and_changed_tag_are_rejected(self):
        for gate in ('native', 'freebsd', 'freebsd_upgrade', 'connectivity'):
            with self.subTest(gate=gate), self.assertRaises(ValueError):
                self.evidence_fixture(connectivity=True, bad_gate=gate)
        with self.assertRaises(ValueError):
            self.evidence_fixture(connectivity=False, tag_sha='f' * 40)
        self.published['prerelease'] = True
        with self.assertRaises(ValueError):
            self.evidence_fixture(connectivity=False)


if __name__ == '__main__':
    unittest.main()
