"""No root/SELinux needed: exercise the privileged source admission boundary."""
import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('selinux_root', ROOT/'ci/selinux_root.py')
helper = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(helper)
NATIVE_SPEC = importlib.util.spec_from_file_location('selinux_native', ROOT/'ci/selinux_native.py')
native = importlib.util.module_from_spec(NATIVE_SPEC)
NATIVE_SPEC.loader.exec_module(native)
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
    @unittest.skipUnless(native.os.name == 'posix', 'POSIX DAC mode required')
    def test_web_source_is_public_code_under_private_root_umask(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / 'source'
            previous = native.os.umask(0o077)
            try:
                native.stage_web_source(target)
            finally:
                native.os.umask(previous)
            # A separate service uid needs search/read access to code; the
            # surrounding fixture data and root work tree remain private.
            self.assertEqual(target.stat().st_mode & 0o777, 0o755)
            self.assertTrue((target / 'webui' / '__main__.py').is_file())
            self.assertEqual((target / 'webui').stat().st_mode & 0o005, 0o005)
            target.chmod(0o700)
            with self.assertRaises(FileExistsError):
                native.stage_web_source(target)
            self.assertEqual(target.stat().st_mode & 0o777, 0o700)

    def test_only_fixture_avc_denials_leave_private_audit_log(self):
        allowed = 'type=AVC msg=audit(1:1): avc:  denied { read } scontext=system_u:system_r:liner_notes_web_t:s0 tcontext=system_u:object_r:liner_notes_data_t:s0'
        unrelated = allowed.replace('liner_notes_web_t', 'httpd_t')
        argument = 'type=EXECVE msg=audit(1:1): a0="private-argument"'
        path = 'type=PATH msg=audit(1:1): name="private-path"'
        impersonation = 'type=AVC msg=audit(1:1): avc: denied { read } name="liner_notes_web_t" scontext=system_u:system_r:httpd_t:s0'
        permitted = allowed.replace('denied', 'granted')
        self.assertEqual(list(native.fixture_denials('\n'.join(
            [allowed, unrelated, argument, path, impersonation, permitted]))), [allowed])
        self.assertEqual(list(native.fixture_denials(allowed + 'x' * 5000)), [(allowed + 'x' * 5000)[:4096]])

    def test_fixture_uid_is_not_the_ssh_callers_writable_identity(self):
        caller = SimpleNamespace(pw_uid=1002)
        valid = dict(pw_uid=990, pw_gid=990, pw_shell='/sbin/nologin')
        helper.validate_account(SimpleNamespace(**valid), caller, [990], [1002])
        for change, own_groups, caller_groups in (
            ({'pw_uid': 0}, [990], [1002]),
            ({'pw_uid': 1002}, [990], [1002]),
            ({'pw_gid': 0}, [990], [1002]),
            ({'pw_shell': '/bin/bash'}, [990], [1002]),
            ({}, [990, 0], [1002]),
            ({}, [990], [1002, 990]),
        ):
            with self.subTest(change=change), self.assertRaises(ValueError):
                helper.validate_account(SimpleNamespace(**(valid | change)), caller, own_groups, caller_groups)

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
