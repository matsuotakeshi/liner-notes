"""Host-independent tests for the SELinux install boundary and label helper."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('labels', ROOT / 'linux/selinux/selinux_labels.py')
labels = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(labels)


class LabelTests(unittest.TestCase):
    def test_inspection_never_changes_labels_or_services(self):
        with patch('sys.argv', ['labels']), patch.object(labels.subprocess, 'run') as run, \
                patch.object(labels.subprocess, 'check_output', return_value='200 liner_notes pp\n400 local pp\n'):
            labels.main()
        self.assertEqual(run.call_args_list[0].args[0], ['/usr/sbin/getenforce'])
        self.assertEqual(run.call_count, 1)

    def test_relabel_is_explicit_and_preserves_local_contexts(self):
        with patch('sys.argv', ['labels', '--relabel']), patch.object(labels.os, 'geteuid', return_value=0, create=True), \
                patch.object(labels, 'existing_paths', return_value=['/var/lib/liner-notes']), \
                patch.object(labels.subprocess, 'run') as run:
            labels.main()
        run.assert_called_once_with(['/usr/sbin/restorecon', '-R', '-v', '/var/lib/liner-notes'], check=True)

    def test_non_root_cannot_relabel(self):
        with patch('sys.argv', ['labels', '--relabel']), patch.object(labels.os, 'geteuid', return_value=123, create=True), \
                patch.object(labels.subprocess, 'run') as run, self.assertRaises(SystemExit):
            labels.main()
        run.assert_not_called()

    def test_missing_paths_are_not_created(self):
        with tempfile.TemporaryDirectory() as tmp:
            missing = Path(tmp) / 'absent'
            with patch.object(labels, 'PATHS', [str(missing)]):
                self.assertEqual(labels.existing_paths(), [])
            self.assertFalse(missing.exists())

    def test_symlinks_are_rejected(self):
        with patch.object(labels, 'PATHS', ['/var/lib/liner-notes']), \
                patch.object(labels.Path, 'is_symlink', return_value=True):
            with self.assertRaisesRegex(ValueError, 'symlink'):
                labels.existing_paths()


if __name__ == '__main__':
    unittest.main()
