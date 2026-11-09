"""Receipt publication retains native flock and works with Solaris leases."""
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('resumecheck', ROOT / 'ports/test-resume.py')
receipt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(receipt)
locks = receipt.posix_locks
FIRST = dict(patch='crc32-lp64-v1', sha256='a' * 64)
SECOND = dict(FIRST, sha256='b' * 64)


@unittest.skipUnless(os.name == 'posix', 'POSIX descriptors required')
class ReceiptTests(unittest.TestCase):
    def modes(self):
        return (True,) if sys.platform.startswith('sunos') else (False, True)

    def test_atomic_update_backup_and_reuse(self):
        for record in self.modes():
            with self.subTest(record=record), tempfile.TemporaryDirectory() as tmp, patch.object(locks, 'RECORD_LOCKS', record):
                root = Path(tmp); path = root / 'receipt.json'
                self.assertEqual(receipt.save_receipt(path, FIRST), 'created')
                original = path.read_bytes()
                self.assertEqual(receipt.save_receipt(path, FIRST), 'unchanged')
                with patch.object(receipt.os, 'replace', side_effect=OSError('injected')):
                    with self.assertRaises(OSError):
                        receipt.save_receipt(path, SECOND)
                self.assertEqual(path.read_bytes(), original)
                self.assertEqual(receipt.save_receipt(path, SECOND), 'updated')
                self.assertEqual(json.loads(path.read_bytes()), SECOND)
                self.assertTrue(all(p.read_bytes() == original for p in root.glob('receipt.json.bak-*')))
                self.assertFalse(list(root.glob('.resume-*')))
                self.assertEqual(path.stat().st_mode & 0o777, 0o600)
                if record:
                    lock = root / locks.NAMESPACE_FILE
                    inode = lock.stat().st_ino
                    self.assertEqual(lock.read_bytes(), b'')
                    self.assertEqual(receipt.save_receipt(path, SECOND), 'unchanged')
                    self.assertEqual(lock.stat().st_ino, inode)

    def test_concurrent_publication_serializes(self):
        for record in self.modes():
            with self.subTest(record=record), tempfile.TemporaryDirectory() as tmp, patch.object(locks, 'RECORD_LOCKS', record):
                path = Path(tmp) / 'receipt.json'
                with ThreadPoolExecutor(max_workers=2) as pool:
                    results = list(pool.map(lambda _: receipt.save_receipt(path, FIRST), range(2)))
                self.assertEqual(sorted(results), ['created', 'unchanged'])
                self.assertEqual(json.loads(path.read_bytes()), FIRST)
                self.assertFalse(list(Path(tmp).glob('*.bak-*')))

    def test_unsafe_record_namespace_is_rejected_unchanged(self):
        for kind in ('symlink', 'hardlink', 'public', 'payload'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as tmp, patch.object(locks, 'RECORD_LOCKS', True):
                root = Path(tmp); path = root / 'receipt.json'; lock = root / locks.NAMESPACE_FILE
                path.write_text('previous'); path.chmod(0o600)
                target = root / 'private'; target.write_text('preserve'); target.chmod(0o600)
                if kind == 'symlink': lock.symlink_to(target)
                elif kind == 'hardlink': os.link(target, lock)
                else:
                    lock.write_bytes(b'data' if kind == 'payload' else b'')
                    lock.chmod(0o666 if kind == 'public' else 0o600)
                with self.assertRaises((OSError, ValueError)):
                    receipt.save_receipt(path, FIRST)
                self.assertEqual(path.read_text(), 'previous')
                self.assertEqual(target.read_text(), 'preserve')
                self.assertFalse(list(root.glob('.resume-*')))

    def test_reserved_name_cannot_replace_lock(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(locks, 'RECORD_LOCKS', True):
            path = Path(tmp) / locks.NAMESPACE_FILE
            with self.assertRaises(ValueError): receipt.save_receipt(path, FIRST)
            self.assertFalse(path.exists())
            with locks.namespace_lock(tmp): inode = path.stat().st_ino
            with self.assertRaises(ValueError): receipt.save_receipt(path, FIRST)
            self.assertEqual(path.stat().st_ino, inode)
            self.assertEqual(path.read_bytes(), b'')

    def test_installed_script_import_is_independent_of_working_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); scripts = root / 'share/liner-notes/scripts'; scripts.mkdir(parents=True)
            tool = scripts / 'check-resume.py'
            shutil.copyfile(ROOT / 'ports/test-resume.py', tool)
            shutil.copyfile(ROOT / 'scripts/posix_locks.py', scripts / 'posix_locks.py')
            result = subprocess.run([sys.executable, '-I', str(tool), '--help'], cwd=root,
                                    capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            code = "import runpy,sys;d=runpy.run_path(sys.argv[1]);d['posix_locks'].RECORD_LOCKS=True;print(d['save_receipt'](sys.argv[2],{'ok':True}))"
            result = subprocess.run([sys.executable, '-I', '-c', code, str(tool), str(root / 'receipt.json')],
                                    cwd=root, capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), 'created')
            self.assertEqual(json.loads((root / 'receipt.json').read_text()), {'ok': True})


if __name__ == '__main__':
    unittest.main()
