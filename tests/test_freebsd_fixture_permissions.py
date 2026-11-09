"""Real filesystem/child-process checks; hosted root uses private temp trees."""
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'ci'))
from freebsd_fixture_permissions import package_install_argv, repair_legacy_sbin


@unittest.skipUnless(os.name == 'posix', 'POSIX modes and shell run on hosted Linux')
class FixturePermissions(unittest.TestCase):
    def setUp(self):
        self.assertEqual(os.geteuid(), 0, 'Hosted fixture ownership checks require sudo')
        self.mask = os.umask(0o077)
        self.temporary = tempfile.TemporaryDirectory(prefix='liner-public-dirs-')
        self.private = Path(self.temporary.name)
        self.jail = self.private/'new-jail'
        (self.jail/'usr/local/sbin').mkdir(parents=True)

    def tearDown(self):
        self.temporary.cleanup()
        os.umask(self.mask)

    def test_only_observed_public_directory_changes_and_repair_is_idempotent(self):
        secrets = [self.jail/'etc/liner-notes/key', self.jail/'var/db/liner-notes/data',
                   self.jail/'var/spool/liner-notes/pending', self.private/'host-key']
        for secret in secrets:
            secret.parent.mkdir(parents=True, exist_ok=True)
            secret.write_bytes(b'private')
        before = {p: (stat.S_IMODE(p.lstat().st_mode), p.lstat().st_uid)
                  for p in [self.private, self.jail, *self.jail.rglob('*')]}
        self.assertEqual(repair_legacy_sbin(self.jail), ['usr/local/sbin'])
        self.assertEqual(repair_legacy_sbin(self.jail), [])
        for path, metadata in before.items():
            expected = (0o755, 0) if path == self.jail/'usr/local/sbin' else metadata
            self.assertEqual((stat.S_IMODE(path.lstat().st_mode), path.lstat().st_uid), expected)
        for secret in secrets:
            self.assertEqual(secret.read_bytes(), b'private')
            self.assertEqual(stat.S_IMODE(secret.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(self.private.stat().st_mode), 0o700)

    def test_existing_public_modes_are_preserved(self):
        path = self.jail/'usr/local/sbin'
        for mode in (0o555, 0o755):
            path.chmod(mode)
            self.assertEqual(repair_legacy_sbin(self.jail), [])
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), mode)

    def test_leaf_symlink_and_file_are_rejected_without_target_changes(self):
        path = self.jail/'usr/local/sbin'
        path.rmdir()
        outside = self.private/'outside'
        outside.mkdir()
        path.symlink_to(outside, target_is_directory=True)
        with self.assertRaises(ValueError):
            repair_legacy_sbin(self.jail)
        self.assertEqual(stat.S_IMODE(outside.stat().st_mode), 0o700)
        path.unlink()
        path.write_bytes(b'not a directory')
        with self.assertRaises(ValueError):
            repair_legacy_sbin(self.jail)
        self.assertEqual(path.read_bytes(), b'not a directory')

    def test_symlink_ancestor_and_relative_root_are_rejected(self):
        alias = self.private/'alias'
        alias.symlink_to(self.jail, target_is_directory=True)
        with self.assertRaises(ValueError):
            repair_legacy_sbin(alias)
        with self.assertRaises(ValueError):
            repair_legacy_sbin(Path('relative-jail'))
        self.assertEqual(stat.S_IMODE((self.jail/'usr/local/sbin').stat().st_mode), 0o700)

    def test_nonroot_owner_and_writable_ancestor_are_rejected(self):
        ancestor = self.jail/'usr/local'
        os.chown(ancestor, 65534, 65534)
        with self.assertRaises(ValueError):
            repair_legacy_sbin(self.jail)
        os.chown(ancestor, 0, 0)
        ancestor.chmod(0o777)
        with self.assertRaises(ValueError):
            repair_legacy_sbin(self.jail)
        self.assertEqual(stat.S_IMODE((self.jail/'usr/local/sbin').stat().st_mode), 0o700)

    def test_unexpected_modes_are_rejected(self):
        path = self.jail/'usr/local/sbin'
        for mode in (0o750, 0o777, 0o4700):
            path.chmod(mode)
            with self.assertRaises(ValueError):
                repair_legacy_sbin(self.jail)
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), mode)

    def test_package_child_uses_022_and_parent_keeps_private_mask(self):
        code = """
import json, pathlib, sys
pathlib.Path('public-dir').mkdir()
pathlib.Path('public-file').write_text('package payload')
print(json.dumps(sys.argv[1:]))
"""
        literal = ['name with spaces', '$(touch injected)', '; false', 'private\\name']
        result = subprocess.run(package_install_argv([sys.executable, '-c', code, *literal]),
                                cwd=self.private, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), literal)
        self.assertFalse((self.private/'injected').exists())
        self.assertEqual(stat.S_IMODE((self.private/'public-dir').stat().st_mode), 0o755)
        self.assertEqual(stat.S_IMODE((self.private/'public-file').stat().st_mode), 0o644)
        following = self.private/'following-private-file'
        following.write_bytes(b'private')
        self.assertEqual(stat.S_IMODE(following.stat().st_mode), 0o600)
        previous = os.umask(0o077)
        self.assertEqual(previous, 0o077)


if __name__ == '__main__':
    unittest.main()
