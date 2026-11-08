"""No root/SELinux needed: exercise the privileged source admission boundary."""
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('selinux_root', ROOT/'ci/selinux_root.py')
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)
SHA = 'a' * 40


def archive(entries):
    output = io.BytesIO()
    with tarfile.open(fileobj=output, mode='w:gz') as stream:
        for name, kind in entries:
            info = tarfile.TarInfo(name)
            info.type = kind
            if kind == tarfile.REGTYPE:
                info.size = 4
                info.mode = 0o777
                stream.addfile(info, io.BytesIO(b'test'))
            else:
                info.linkname = '/etc/shadow'
                stream.addfile(info)
    return output.getvalue()


class RootBoundaryTests(unittest.TestCase):
    def test_argument_is_only_exact_sha(self):
        self.assertEqual(helper.validate_sha(SHA), SHA)
        for value in ('main', SHA+';id', '../source', SHA.upper(), SHA+'\n', ''):
            with self.subTest(value=value), self.assertRaises(ValueError):
                helper.validate_sha(value)

    def test_main_lookup_is_fixed_repository_not_supplied_url(self):
        with patch.object(helper, 'get', return_value=b'{"object":{"sha":"'+SHA.encode()+b'"}}') as get:
            self.assertEqual(helper.current_main(), SHA)
        self.assertEqual(get.call_args.args[0],
                         'https://api.github.com/repos/matsuotakeshi/liner-notes/git/ref/heads/main')

    def test_rejects_unsafe_archives_before_any_write(self):
        prefix = 'liner-notes-'+SHA
        for entries in (
            [(prefix+'/good', tarfile.REGTYPE), (prefix+'/../escape', tarfile.REGTYPE)],
            [(prefix+'/link', tarfile.SYMTYPE)],
            [(prefix+'/hard', tarfile.LNKTYPE)],
            [(prefix+'/fifo', tarfile.FIFOTYPE)],
            [(prefix+'/file', tarfile.REGTYPE)]*2,
            [('/absolute', tarfile.REGTYPE)],
            [('other-source/file', tarfile.REGTYPE)],
            [(prefix+'/a\\b', tarfile.REGTYPE)],
        ):
            with tempfile.TemporaryDirectory() as directory:
                target = Path(directory)
                with self.assertRaises(ValueError):
                    helper.extract(archive(entries), target, SHA)
                self.assertEqual(list(target.iterdir()), [])

    def test_valid_source_and_writable_bits_removed(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            helper.extract(archive([('liner-notes-'+SHA+'/ci/test', tarfile.REGTYPE)]), target, SHA)
            self.assertEqual((target/'ci/test').read_bytes(), b'test')
            # POSIX chmod semantics are checked on the hosted runner.
            if helper.os.name == 'posix':
                self.assertEqual((target/'ci/test').stat().st_mode & 0o777, 0o755)


if __name__ == '__main__':
    unittest.main()
