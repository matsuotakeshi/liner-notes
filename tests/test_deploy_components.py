"""Exact-SHA optional component evidence, without SSH or package execution."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'ci'), str(ROOT / 'scripts'), str(ROOT / 'tests')]
from deploy_release import package_evidence, select_artifact
from deploy_manifest import release_document
from test_deployment import release


class Components(unittest.TestCase):
    def setUp(self):
        self.release = release()
        self.manifests, self.receipt = {}, dict(source_sha=self.release['source_sha'], version='0.75', platforms={}, components={})
        for platform, packages in self.release['platforms'].items():
            manifest = dict(platform=platform, source_sha=self.release['source_sha'], version='0.75-1', packages={
                name: {k: v for k, v in p.items() if k != 'version'} for name, p in packages.items()})
            self.manifests[platform] = manifest
            self.receipt['platforms'][platform] = dict(version='0.75-1', packages={
                name: {k: p[k] for k in ('filename', 'sha256')} for name, p in packages.items()})
        self.policy = dict(platform='rocky10-selinux', source_sha=self.release['source_sha'], version='0.1.0-1.el10',
                           packages={'liner-notes-selinux': dict(filename='liner-notes-selinux-0.1.0-1.el10.noarch.rpm', size=36032, sha256='c'*64)})

    def selected(self):
        self.release['platforms']['rocky10']['liner-notes-selinux'] = dict(
            self.policy['packages']['liner-notes-selinux'], version=self.policy['version'])
        self.receipt['components']['rocky10-selinux'] = {k: copy.deepcopy(self.policy[k]) for k in ('source_sha', 'version', 'packages')}

    def test_default_core_and_selected_independent_version(self):
        package_evidence(self.release, self.receipt, self.manifests)
        self.selected()
        package_evidence(self.release, self.receipt, self.manifests, self.policy)

    def test_policy_requires_its_own_manifest_and_receipt(self):
        self.selected()
        for value in (None, self.manifests['rocky10'], dict(self.policy, source_sha='d'*40), dict(self.policy, version='0.75')):
            with self.assertRaises(ValueError):
                package_evidence(self.release, self.receipt, self.manifests, value)
        for field, value in [('components', {}), ('source_sha', 'd'*40)]:
            broken = copy.deepcopy(self.receipt)
            broken[field] = value
            with self.assertRaises(ValueError):
                package_evidence(self.release, broken, self.manifests, self.policy)
        for key, value in [('size', 1), ('sha256', 'e'*64), ('filename', 'other.rpm')]:
            broken = copy.deepcopy(self.policy)
            broken['packages']['liner-notes-selinux'][key] = value
            with self.assertRaises(ValueError):
                package_evidence(self.release, self.receipt, self.manifests, broken)
            broken_receipt = copy.deepcopy(self.receipt)
            broken_receipt['components']['rocky10-selinux']['packages']['liner-notes-selinux'][key] = value
            with self.assertRaises(ValueError):
                package_evidence(self.release, broken_receipt, self.manifests, self.policy)
        for key, value in [('source_sha', 'd'*40), ('version', '0.75')]:
            broken_receipt = copy.deepcopy(self.receipt)
            broken_receipt['components']['rocky10-selinux'][key] = value
            with self.assertRaises(ValueError):
                package_evidence(self.release, broken_receipt, self.manifests, self.policy)

    def test_core_package_versions_still_bound(self):
        self.manifests['rocky10']['version'] = '0.74.1'
        with self.assertRaises(ValueError):
            package_evidence(self.release, self.receipt, self.manifests)

    def test_artifact_cannot_come_from_other_attempt_or_revision(self):
        artifact = dict(name='liner-notes-selinux-policy', expired=False, workflow_run=dict(id=123, head_sha='a'*40))
        self.assertIs(select_artifact(dict(artifacts=[artifact]), artifact['name'], 123, 'a'*40), artifact)
        for candidate in [dict(artifact, expired=True), dict(artifact, workflow_run=dict(id=122, head_sha='a'*40)),
                          dict(artifact, workflow_run=dict(id=123, head_sha='b'*40))]:
            with self.assertRaises(ValueError):
                select_artifact(dict(artifacts=[candidate]), artifact['name'], 123, 'a'*40)

    def test_signing_component_is_explicit_and_requires_verified_set(self):
        self.selected()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for platform, manifest in self.manifests.items():
                (root/platform).mkdir()
                (root/platform/'RELEASE-MANIFEST.json').write_text(json.dumps(manifest))
            with patch('deploy_manifest.verify_set', return_value=self.receipt) as verifier:
                document = release_document(root, self.release['source_sha'], 123)
                self.assertNotIn('liner-notes-selinux', document['platforms']['rocky10'])
                verifier.assert_called_once_with(root, self.release['source_sha'], require_selinux=False)
            with patch('deploy_manifest.verify_set', return_value=self.receipt) as verifier:
                document = release_document(root, self.release['source_sha'], 123, True)
                self.assertEqual(document['platforms']['rocky10']['liner-notes-selinux']['version'], '0.1.0-1.el10')
                verifier.assert_called_once_with(root, self.release['source_sha'], require_selinux=True)
            self.receipt['components'] = {}
            with patch('deploy_manifest.verify_set', return_value=self.receipt), self.assertRaises(ValueError):
                release_document(root, self.release['source_sha'], 123, True)


if __name__ == '__main__':
    unittest.main()
