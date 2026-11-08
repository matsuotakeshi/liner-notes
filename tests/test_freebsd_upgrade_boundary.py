"""Hosted checks of the privileged jail helper; no jail/SSH/root calls."""
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'ci'))
from freebsd_upgrade_root import select_package_run, unpack_package, validate_jail_paths


class Boundary(unittest.TestCase):
    def test_latest_failed_or_pr_build_never_accepted(self):
        sha = 'a'*40
        run = dict(id=10, head_sha=sha, head_branch='main', event='push',
                   run_number=1, run_attempt=1, status='completed', conclusion='success')
        self.assertEqual(select_package_run({'workflow_runs': [run]}, sha)['id'], 10)
        for modified in (dict(run, event='pull_request'), dict(run, head_branch='feature'),
                         dict(run, head_sha='b'*40), dict(run, run_attempt=2, conclusion='failure')):
            with self.assertRaises(ValueError):
                select_package_run({'workflow_runs': [modified]}, sha)
        with self.assertRaises(ValueError):
            select_package_run({'workflow_runs': [run, dict(run, run_attempt=2, conclusion='failure')]}, sha)

    def archive(self, extra=None, source='a'*40, payload=b'pkg', expected=b'pkg'):
        manifest = {'platform': 'freebsd15', 'source_sha': source, 'packages': {
            'liner-notes': {'filename': 'liner-notes-0.75.pkg', 'size': len(expected),
                            'sha256': hashlib.sha256(expected).hexdigest()}}}
        content = io.BytesIO()
        with zipfile.ZipFile(content, 'w') as archive:
            archive.writestr('RELEASE-MANIFEST.json', json.dumps(manifest))
            archive.writestr('liner-notes-0.75.pkg', payload)
            if extra:
                entry = zipfile.ZipInfo('placeholder')
                entry.filename = extra  # preserve malformed separators on Windows too
                archive.writestr(entry, b'unsafe')
        return content.getvalue()

    def test_artifact_digest_package_hash_and_path_guards(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            content = self.archive()
            artifact = {'digest': 'sha256:'+hashlib.sha256(content).hexdigest()}
            unpack_package(content, artifact, 'a'*40, root)
            self.assertEqual((root/'liner-notes-0.75.pkg').read_bytes(), b'pkg')
            for invalid in (self.archive(extra='../outside'), self.archive(extra='/absolute'),
                            self.archive(extra='dir\\outside'), self.archive(source='b'*40),
                            self.archive(payload=b'bad')):
                with self.assertRaises(ValueError):
                    unpack_package(invalid, {'digest': 'sha256:'+hashlib.sha256(invalid).hexdigest()}, 'a'*40, root)
            with self.assertRaises(ValueError):
                unpack_package(content, {'digest': 'sha256:'+'0'*64}, 'a'*40, root)
            with self.assertRaises(ValueError):
                unpack_package(content, {}, 'a'*40, root)

    def test_host_workflow_does_not_contact_lan_for_doc_skip(self):
        workflow = (ROOT/'.github/workflows/freebsd-upgrade-acceptance.yml').read_text()
        self.assertIn("needs.admission.outputs.execute == 'true'", workflow)
        self.assertIn('runs-on: ubuntu-24.04', workflow)
        self.assertNotIn('pull_request:', workflow)
        self.assertIn('StrictHostKeyChecking=yes', workflow)
        self.assertIn('sudo -n /usr/local/libexec/liner-notes-freebsd-upgrade-ci', workflow)
        helper = (ROOT/'ci/freebsd_upgrade_root.py').read_text()
        self.assertIn("'vnet', 'persist'", helper)
        self.assertIn("'allow.mount=0'", helper)
        self.assertNotIn("'reboot'", helper)
        self.assertNotIn("'pkg', 'upgrade'", helper)  # only jail executes package changes
        fixture = (ROOT/'ci/freebsd_upgrade_jail.py').read_text()
        self.assertIn("'security.jail.jailed'", fixture)
        self.assertIn("shutil.move(data, '/ci/new-data-retained')", fixture)

    @unittest.skipUnless(os.name == 'posix', 'Native symlink guards run on hosted Linux')
    def test_dangling_ci_and_dev_mount_symlinks_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root/'dev').mkdir()
            validate_jail_paths(root)
            (root/'ci').symlink_to(root/'absent')
            with self.assertRaises(ValueError):
                validate_jail_paths(root)
            (root/'ci').unlink()
            (root/'dev').rmdir()
            (root/'dev').symlink_to('/dev', target_is_directory=True)
            with self.assertRaises(ValueError):
                validate_jail_paths(root)


if __name__ == '__main__':
    unittest.main()
