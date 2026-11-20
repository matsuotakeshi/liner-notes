"""Portable checks for the approval-gated Solaris identity fixture.

Mocks never constitute a native privilege barrier result. The native test runs
only a non-root reporter; the root matrix requires a separately installed bundle.
"""
import copy
import errno
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'ci'))
import solaris_door_boundary as fixture


def identity():
    return dict(ruid=61001, euid=61001, suid=61001, rgid=61001, egid=61001,
                sgid=61001, groups=0, basic_limit=True)


def cases():
    result = {'plain': {'baseline': identity(), 'basic': identity()}}
    for mode in ('uid', 'gid'):
        for role in ('bbs', 'broker', 'consumer', 'other_slot', 'root'):
            control = identity()
            target = 0 if role == 'root' else fixture.IDS[role]
            for field in (('euid', 'suid') if mode == 'uid' else ('egid', 'sgid')):
                control[field] = target
            result[f'{mode}-{role}'] = {'baseline': control, 'basic': identity()}
    return result


class Boundary(unittest.TestCase):
    def test_plan_has_no_host_mutation_or_enablement(self):
        plan = fixture.plan()
        for key in ('creates_accounts', 'changes_services', 'changes_mounts', 'enables_solaris_door'):
            self.assertFalse(plan[key])
        self.assertEqual(len(set(plan['uids_and_gids'].values())), 5)

    def test_prepare_is_new_directory_only_and_hashes_exact_sources(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)/'bundle'
            fixture.prepare(out, 'a'*40)
            manifest = json.loads((out/'manifest.json').read_text())
            self.assertEqual(manifest['source_sha'], 'a'*40)
            self.assertEqual(set(manifest['sha256']), set(fixture.FILES))
            for name, digest in manifest['sha256'].items():
                self.assertEqual(hashlib.sha256((out/name).read_bytes()).hexdigest(), digest)
            with self.assertRaises(FileExistsError): fixture.prepare(out, 'a'*40)
            with self.assertRaises(ValueError): fixture.prepare(Path(temporary)/'bad', 'main')
            self.assertFalse((Path(temporary)/'bad').exists())

    def test_safe_identity_observation_never_enables_backend(self):
        result = fixture.evaluate(cases())
        self.assertTrue(result['setid_confined'])
        self.assertFalse(result['broker_isolation_verified'])
        self.assertFalse(result['door_backend_enabled'])

    def test_effective_or_saved_uid_gid_escape_is_detected(self):
        for field in ('euid', 'suid', 'egid', 'sgid'):
            matrix = cases()
            matrix['uid-bbs']['basic'][field] = 61002
            result = fixture.evaluate(matrix)
            self.assertEqual(result['identity_escapes'], ['uid-bbs'])
            self.assertFalse(result['setid_confined'])

    def test_additional_privileges_are_not_safe_uid_confinement(self):
        matrix = cases()
        matrix['uid-root']['basic']['basic_limit'] = False
        self.assertEqual(fixture.evaluate(matrix)['identity_escapes'], ['uid-root'])

    def test_denied_exec_only_counts_after_working_control(self):
        for number in (errno.EACCES, errno.EPERM):
            matrix = cases()
            matrix['uid-root']['basic'] = {'exec_denied': True, 'errno': number}
            self.assertTrue(fixture.evaluate(matrix)['setid_confined'])
            matrix['uid-root']['baseline'] = copy.deepcopy(matrix['uid-root']['basic'])
            with self.assertRaises(ValueError): fixture.evaluate(matrix)

    def test_partial_matrix_invalid_report_and_inactive_setid_are_inconclusive(self):
        matrix = cases(); del matrix['gid-root']
        with self.assertRaises(ValueError): fixture.evaluate(matrix)
        matrix = cases(); matrix['gid-broker']['baseline'] = identity()
        with self.assertRaises(ValueError): fixture.evaluate(matrix)
        for payload in ({}, {'exec_denied': True, 'errno': errno.ENOENT},
                        {'exec_denied': True, 'errno': True},
                        dict(identity(), euid=True), dict(identity(), basic_limit=1)):
            with self.assertRaises(ValueError): fixture.parse_report(json.dumps(payload), allow_denied=True)

    def test_wrong_real_id_and_supplementary_groups_refuse_observation(self):
        for field in ('ruid', 'rgid', 'groups'):
            matrix = cases(); matrix['plain']['basic'][field] = 1
            with self.assertRaises(ValueError): fixture.evaluate(matrix)

    def test_non_solaris_refuses_before_bundle_or_mutation(self):
        with patch.object(fixture.platform, 'system', return_value='Linux'), \
             patch.object(fixture, 'verify_bundle') as verify, \
             patch.object(fixture.tempfile, 'mkdtemp') as create:
            with self.assertRaises(ValueError): fixture.run()
            verify.assert_not_called(); create.assert_not_called()

    def test_nonroot_and_setuid_invocation_refused(self):
        for real, effective in ((1000, 1000), (1000, 0), (0, 1000)):
            with patch.object(fixture.platform, 'system', return_value='SunOS'), \
                 patch.object(fixture.os, 'getuid', return_value=real, create=True), \
                 patch.object(fixture.os, 'geteuid', return_value=effective, create=True), \
                 patch.object(fixture, 'verify_bundle') as verify:
                with self.assertRaises(ValueError): fixture.run()
                verify.assert_not_called()

    def test_wrong_install_location_refused(self):
        with self.assertRaises(ValueError): fixture.verify_bundle()

    def test_manifest_tampering_and_unlisted_payload_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = fixture.prepare(Path(temporary)/'bundle', 'a'*40)
            with patch.object(fixture, 'INSTALL', out.resolve()), \
                 patch.object(fixture, '__file__', str(out/'solaris_door_boundary.py')), \
                 patch.object(fixture, 'trusted'):
                self.assertEqual(fixture.verify_bundle(), 'a'*40)
                source = out/'solaris_door_identity_probe.c'; original = source.read_bytes()
                source.write_bytes(original+b'/* changed */')
                with self.assertRaises(ValueError): fixture.verify_bundle()
                source.write_bytes(original)
                manifest = json.loads((out/'manifest.json').read_text())
                manifest['sha256']['unexpected.py'] = 'a'*64
                (out/'manifest.json').write_text(json.dumps(manifest))
                with self.assertRaises(ValueError): fixture.verify_bundle()

    def test_untrusted_ownership_writes_links_and_hardlinks_rejected(self):
        for mode, uid, links in ((stat.S_IFREG | 0o666, 0, 1),
                                 (stat.S_IFREG | 0o600, 1000, 1),
                                 (stat.S_IFLNK | 0o600, 0, 1),
                                 (stat.S_IFREG | 0o600, 0, 2)):
            value = Mock(st_mode=mode, st_uid=uid, st_nlink=links)
            with patch.object(fixture.Path, 'lstat', return_value=value):
                with self.assertRaises(ValueError): fixture.trusted('/fixture')

    def test_system_compiler_allows_packaged_aliases_but_not_private_fixture(self):
        value = Mock(st_mode=stat.S_IFREG | 0o555, st_uid=0, st_nlink=3)
        resolved = Path('/usr/gcc/15/bin/gcc')
        with patch.object(fixture.Path, 'resolve', return_value=resolved) as resolve, \
             patch.object(fixture.Path, 'lstat', return_value=value):
            self.assertEqual(fixture.compiler_path(), resolved)
            resolve.assert_called_once_with(strict=True)
            with self.assertRaises(ValueError): fixture.trusted(resolved)

    def test_compiler_aliases_keep_owner_write_and_symlink_refusals(self):
        for mode, uid in ((stat.S_IFREG | 0o775, 0), (stat.S_IFREG | 0o557, 0),
                          (stat.S_IFREG | 0o555, 1000), (stat.S_IFLNK | 0o555, 0)):
            value = Mock(st_mode=mode, st_uid=uid, st_nlink=3)
            with patch.object(fixture.Path, 'resolve', return_value=Path('/compiler')), \
                 patch.object(fixture.Path, 'lstat', return_value=value):
                with self.assertRaises(ValueError): fixture.compiler_path()

    def test_writable_compiler_ancestor_still_rejected(self):
        leaf = Mock(st_mode=stat.S_IFREG | 0o555, st_uid=0, st_nlink=3)
        parent = Mock(st_mode=stat.S_IFDIR | 0o777, st_uid=0, st_nlink=1)
        with patch.object(fixture.Path, 'resolve', return_value=Path('/unsafe/gcc')), \
             patch.object(fixture.Path, 'lstat', side_effect=[leaf, parent]):
            with self.assertRaises(ValueError): fixture.compiler_path()

    def test_timeout_kills_only_owned_process_group_then_reaps(self):
        process = Mock(pid=123, returncode=0)
        process.communicate.side_effect = [subprocess.TimeoutExpired('fixture', 1), ('', '')]
        with patch.object(fixture.subprocess, 'Popen', return_value=process) as start, \
             patch.object(fixture.signal, 'SIGKILL', 9, create=True), \
             patch.object(fixture.os, 'killpg', create=True) as kill:
            with self.assertRaises(subprocess.TimeoutExpired): fixture.command(['/fixed/probe'], timeout=1)
            self.assertTrue(start.call_args.kwargs['start_new_session'])
            self.assertTrue(start.call_args.kwargs['close_fds'])
            self.assertEqual(start.call_args.kwargs['env'], fixture.ENV)
            kill.assert_called_once_with(123, fixture.signal.SIGKILL)
            self.assertEqual(process.communicate.call_count, 2)

    def test_cleanup_refuses_links_and_only_removes_explicit_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); owned = root/'fixture'; unrelated = root/'keep'
            owned.write_text('synthetic'); unrelated.write_text('preserve')
            fixture.remove_fixture_files([owned])
            self.assertFalse(owned.exists()); self.assertEqual(unrelated.read_text(), 'preserve')
            with self.assertRaises(ValueError): fixture.remove_fixture_files([root])

    @unittest.skipUnless(sys.platform.startswith('sunos'), 'native Solaris compiler and credentials')
    def test_native_reporter_without_root(self):
        self.assertNotEqual(os.getuid(), 0, 'normal native CI must use its unprivileged account')
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary)/'probe'
            # Exercise the root fixture's exact read-only compiler preflight as
            # the normal CI user, before any separately approved root execution.
            subprocess.run([str(fixture.compiler_path()), '-m64', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(ROOT/'tests/solaris_door_identity_probe.c'), '-o', str(binary)],
                           check=True, timeout=45)
            result = subprocess.run([str(binary), '--report'], capture_output=True, text=True, check=True, timeout=5)
            value = fixture.parse_report(result.stdout)
            self.assertEqual(value['ruid'], os.getuid())
            self.assertEqual(value['euid'], os.geteuid())
            self.assertEqual(value['rgid'], os.getgid())
            for mode in ('--inventory', '--basic', '--baseline'):
                result = subprocess.run([str(binary), mode, str(binary)], timeout=5)
                self.assertEqual(result.returncode, 77)


if __name__ == '__main__': unittest.main()
