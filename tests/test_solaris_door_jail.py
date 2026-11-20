"""Portable guard/evidence tests; never substitutes for approved Solaris root run."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'ci'))
import solaris_door_jail as fixture


def matrix():
    rows = {}
    for name in fixture.CASES:
        before = dict.fromkeys(fixture.ID_FIELDS,61001)
        before.update(groups=0,basic_limit=False)
        if name != 'plain':
            kind, role = name.split('-')
            for field in (('euid','suid') if kind == 'uid' else ('egid','sgid')):
                before[field] = fixture.ROLES[role]
        after = dict.fromkeys(fixture.ID_FIELDS,61001)
        after.update(groups=0,**dict.fromkeys(fixture.FLAGS,True))
        rows[name] = {'baseline':before,'jailed':after}
    return rows


class JailFixture(unittest.TestCase):
    def test_default_plan_does_not_authorize_execution(self):
        result = subprocess.run([sys.executable,str(ROOT/'ci/solaris_door_jail.py')],
                                check=True,capture_output=True,text=True,timeout=10)
        value = json.loads(result.stdout)
        self.assertTrue(value['default_read_only'])
        self.assertTrue(value['requires_separate_mount_approval'])
        self.assertFalse(value['door_backend_enabled'])
        self.assertEqual(value['mount']['options'],'nosuid,size=64m')

    def test_prepare_hashes_all_sources_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = fixture.prepare(Path(temporary)/'new','a'*40)
            manifest = json.loads((out/'manifest.json').read_text())
            for name in fixture.SOURCES:
                self.assertEqual(manifest['sha256'][name],hashlib.sha256((out/name).read_bytes()).hexdigest())
            with self.assertRaises(FileExistsError): fixture.prepare(out,'a'*40)
            with self.assertRaises(ValueError): fixture.prepare(Path(temporary)/'bad','main')

    def test_bundle_verified_before_import_or_root_side_effect(self):
        with self.assertRaises(ValueError): fixture.verify_bundle()
        with patch.object(fixture.platform,'system',return_value='Linux'), patch.object(fixture,'verify_bundle') as verify:
            with self.assertRaises(ValueError): fixture.run()
            verify.assert_not_called()
        for uid, euid in ((1000,1000),(1000,0),(0,1000)):
            with patch.object(fixture.platform,'system',return_value='SunOS'), \
                 patch.object(fixture.os,'getuid',return_value=uid,create=True), \
                 patch.object(fixture.os,'geteuid',return_value=euid,create=True), \
                 patch.object(fixture,'verify_bundle') as verify:
                with self.assertRaises(ValueError): fixture.run()
                verify.assert_not_called()

    def test_tampered_bundle_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = fixture.prepare(Path(temporary)/'new','a'*40).resolve()
            with patch.object(fixture,'INSTALL',out), patch.object(fixture,'__file__',str(out/'solaris_door_jail.py')), patch.object(fixture,'trusted'):
                self.assertEqual(fixture.verify_bundle()['source_sha'],'a'*40)
                (out/'solaris_door_boundary.py').write_text('untrusted')
                with self.assertRaises(ValueError): fixture.verify_bundle()

    def test_mount_options_and_nested_mounts_fail_closed(self):
        root = '/var/tmp/liner-door-jail-12345678/root'
        options = fixture.mount_entry('swap '+root+' tmpfs rw,nosuid,size=64m 0\n',root)
        fixture.require_nosuid(options)
        fixture.require_nosuid({'rw','nosetuid','nodevices'})
        for bad in (None,set(),{'nosetuid'},{'nosuid','setuid'},{'nosuid','noexec'},{'nosuid','devices'}):
            with self.assertRaises(ValueError): fixture.require_nosuid(bad)
        for text in ('disk '+root+' zfs rw 0', 'swap '+root+'/proc tmpfs rw 0',
                     ('swap '+root+' tmpfs nosuid 0\n')*2):
            with self.assertRaises(ValueError): fixture.mount_entry(text,root)
        self.assertIsNone(fixture.mount_entry('swap /tmp tmpfs rw 0',root))

    def test_library_inventory_strict_allowlist(self):
        paths = fixture.library_paths(' libc.so.1 => /lib/64/libc.so.1\n')
        self.assertEqual(len(paths),2)
        for text in ('','libc.so.1 => (file not found)', 'libc => /etc/libc.so',
                     'libc => /lib/64/../../etc/passwd','libc => /lib/64/libc.so trailing'):
            with self.assertRaises(ValueError): fixture.library_paths(text)

    def test_success_is_only_credential_namespace_result(self):
        result = fixture.evaluate(matrix())
        self.assertTrue(result['credential_namespace_confined'])
        for key in ('broker_isolation_verified','door_backend_enabled','lifecycle_verified'):
            self.assertFalse(result[key])

    def test_every_identity_and_namespace_escape_detected(self):
        for key in fixture.ID_FIELDS | {'groups'} | fixture.FLAGS:
            rows = matrix()
            rows['uid-bbs']['jailed'][key] = False if key in fixture.FLAGS else 61002
            self.assertEqual(fixture.evaluate(rows)['failed_cases'],['uid-bbs'])

    def test_denied_exec_needs_successful_plain_and_setid_controls(self):
        rows = matrix(); rows['uid-root']['jailed'] = {'exec_denied':True}
        self.assertTrue(fixture.evaluate(rows)['credential_namespace_confined'])
        rows['plain']['jailed'] = {'exec_denied':True}
        with self.assertRaises(ValueError): fixture.evaluate(rows)
        rows = matrix(); rows['gid-bbs']['baseline']['egid'] = 61001
        with self.assertRaises(ValueError): fixture.evaluate(rows)

    def test_incomplete_or_mistyped_evidence_not_success(self):
        rows = matrix(); del rows['gid-root']
        with self.assertRaises(ValueError): fixture.evaluate(rows)
        for value in ({}, {'exec_denied':1}, {'exec_denied':True,'errno':2},
                      dict(matrix()['plain']['jailed'],namespace_closed=1)):
            rows = matrix(); rows['plain']['jailed'] = value
            with self.assertRaises(ValueError): fixture.evaluate(rows)

    def test_command_failure_and_timeout_leave_bounded_evidence(self):
        process = Mock(pid=234,returncode=9)
        process.communicate.return_value = ('synthetic output','loader problem')
        with patch.object(fixture.subprocess,'Popen',return_value=process) as start:
            report = {}
            with self.assertRaises(subprocess.CalledProcessError): fixture.command(['/fixed/probe'],report)
            self.assertEqual(report['commands'][0]['stderr'],'loader problem')
            self.assertTrue(start.call_args.kwargs['close_fds'])
        process.communicate.side_effect = [subprocess.TimeoutExpired('probe',1),('','x'*70000)]
        with patch.object(fixture.subprocess,'Popen',return_value=process), \
             patch.object(fixture.os,'killpg',create=True) as kill, \
             patch.object(fixture.signal,'SIGKILL',9,create=True):
            report = {}
            with self.assertRaises(subprocess.TimeoutExpired): fixture.command(['/fixed/probe'],report)
            self.assertEqual(len(report['commands'][0]['stderr']),65536)
            self.assertTrue(report['commands'][0]['timeout']); kill.assert_called_once_with(234,9)

    def failed_run(self, mounted=False, unmount_failure=False):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)/'private'; work.mkdir()
            base = Mock()
            base.compiler_path.return_value = Path('/compiler')
            def remove(paths):
                for path in paths:
                    path.chmod(0o600); path.unlink()
            base.remove_fixture_files.side_effect = remove
            calls = []
            def fake_command(args, report, timeout=15):
                calls.append([str(a).replace(chr(92), '/') for a in args])
                if Path(args[0]) == Path('/compiler'):
                    Path(args[-1]).write_text('synthetic')
                    if not mounted: raise subprocess.CalledProcessError(1,args)
                if str(args[0]).endswith('umount') and unmount_failure:
                    raise subprocess.CalledProcessError(1,args)
                output = 'libc.so.1 => /lib/64/libc.so.1\n' if str(args[0]).endswith('ldd') else ''
                return subprocess.CompletedProcess(args,0,output,'')
            sequence = [None,{'setuid'},{'setuid'}]
            if not unmount_failure: sequence.append(None)
            with patch.object(fixture.tempfile,'mkdtemp',return_value=str(work)), \
                 patch.object(fixture.Path,'resolve',lambda self,**kwargs:self), \
                 patch.object(fixture,'read_mount',side_effect=sequence), \
                 patch.object(fixture,'command',side_effect=fake_command):
                with self.assertRaises((ValueError,subprocess.CalledProcessError)):
                    fixture.run_locked(base,{'source_sha':'a'*40,'sha256':{}})
            result = json.loads((work/'result.json').read_text())
            self.assertFalse(result['broker_isolation_verified'])
            if unmount_failure:
                self.assertFalse(result['cleanup_complete'])
                self.assertEqual(result['cleanup_error'],'CalledProcessError')
                self.assertTrue((work/'root').exists())
                self.assertTrue((work/'observer').exists())
            else:
                self.assertTrue(result['cleanup_complete'])
                self.assertEqual({p.name for p in work.iterdir()},{'plan.json','result.json'})
            if not mounted:
                self.assertFalse(any(a[0].endswith('mount') for a in calls))
            else:
                self.assertEqual(sum(a[0].endswith('/umount') for a in calls),1)
                self.assertFalse(any('--observe' in a for a in calls))

    def test_compile_failure_has_receipt_without_mount(self):
        self.failed_run()

    def test_rejected_mount_options_unmount_before_successful_cleanup(self):
        self.failed_run(mounted=True)

    def test_unmount_failure_preserves_paths_and_records_incomplete_cleanup(self):
        self.failed_run(mounted=True,unmount_failure=True)

    @unittest.skipUnless(sys.platform.startswith('sunos'),'Solaris native compilation only, no root/mount')
    def test_native_compiler_and_unprivileged_refusal(self):
        self.assertNotEqual(os.getuid(),0)
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary)/'probe'
            subprocess.run(['/usr/bin/gcc','-m64','-std=c11','-Wall','-Wextra','-Werror',
                            str(ROOT/'tests/solaris_door_jail_probe.c'),'-o',str(binary)],check=True,timeout=45)
            result = subprocess.run([str(binary),'--observe','/','plain'],timeout=5)
            self.assertEqual(result.returncode,80)


if __name__ == '__main__': unittest.main()
