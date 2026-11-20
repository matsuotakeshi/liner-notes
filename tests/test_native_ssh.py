"""Native SSH suite admission/result tests; no private-network connections."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'ci'))
from native_remote import commands_for, RESULT
from native_ci import all_passed, parse_result
import native_ssh


class NativeSsh(unittest.TestCase):
    def test_workflow_keeps_main_only_trust_and_shared_host_lock(self):
        import yaml
        doc = yaml.load((ROOT / '.github/workflows/embedded-ssh-native.yml').read_text(), Loader=yaml.BaseLoader)
        self.assertEqual(set(doc['on']), {'workflow_dispatch'})
        self.assertEqual(doc['concurrency']['group'], 'liner-notes-native-hosts')
        self.assertEqual(doc['concurrency']['cancel-in-progress'], 'false')
        condition = doc['jobs']['ssh']['if']
        self.assertIn("github.ref == 'refs/heads/main'", condition)
        self.assertIn("github.event_name == 'workflow_dispatch'", condition)
        self.assertIn("github.repository == 'matsuotakeshi/liner-notes'", condition)
        command = next(s['run'] for s in doc['jobs']['ssh']['steps'] if 'run' in s)
        self.assertIn('--suite ssh', command)
        self.assertNotIn('${{', command)

    def test_suite_is_distinct_and_does_not_repeat_full_regression(self):
        for target in ('freebsd', 'rocky', 'ubuntu', 'solaris'):
            self.assertEqual(commands_for(target, 'ssh'),
                             [[sys.executable, 'ci/native_ssh.py', '--target', target]])
            self.assertNotEqual(commands_for(target), commands_for(target, 'ssh'))
        with self.assertRaises(ValueError):
            commands_for('freebsd', 'unknown')

    def test_core_receipt_cannot_attest_encrypted_ssh(self):
        sha = 'a' * 40
        row = dict(source_sha=sha, target='freebsd', status='passed')
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / 'worker.log'
            for suite in (None, 'core', 'ssh'):
                record = dict(row, **({'suite': suite} if suite else {}))
                log.write_text(RESULT + json.dumps(record), encoding='utf8')
                self.assertEqual(all_passed([record], ['freebsd'], sha, 'ssh'), suite == 'ssh')
                if suite == 'ssh':
                    parse_result(log, 'freebsd', sha, 'ssh')
                else:
                    with self.assertRaisesRegex(ValueError, 'suite'):
                        parse_result(log, 'freebsd', sha, 'ssh')

    def test_private_solaris_library_is_scoped_and_keeps_system_fallback(self):
        with tempfile.TemporaryDirectory() as temporary, \
             patch.object(native_ssh.Path,'home',return_value=Path(temporary)), \
             patch.dict(os.environ,{'PKG_CONFIG_PATH':'/existing'},clear=True):
            self.assertIsNone(native_ssh.private_solaris_dependency('solaris'))
            prefix=Path(temporary)/'liner-notes-deps/libssh-0.12.2/local'
            metadata=prefix/'lib/pkgconfig/libssh.pc'
            metadata.parent.mkdir(parents=True);metadata.write_text('synthetic')
            self.assertIsNone(native_ssh.private_solaris_dependency('ubuntu'))
            self.assertEqual(os.environ['PKG_CONFIG_PATH'],'/existing')
            self.assertEqual(native_ssh.private_solaris_dependency('solaris'),prefix)
            self.assertEqual(os.environ['PKG_CONFIG_PATH'],str(metadata.parent)+os.pathsep+'/existing')

    def test_solaris_binary_records_private_library_runpath(self):
        with patch.object(native_ssh.shutil,'which',return_value='pkg-config'), \
             patch.object(native_ssh.subprocess,'run',return_value=Mock(returncode=0)), \
             patch.object(native_ssh.subprocess,'check_output',side_effect=['-I/private/include','-L/private/lib -lssh']), \
             patch('solaris_ci.build_arguments',return_value=['gmake','CFLAGS=-m64']):
            command=native_ssh.build_command('solaris')
        self.assertIn('CFLAGS=-m64',command)
        self.assertIn('SSH_LIBS=-L/private/lib -lssh -Wl,-R,/private/lib',command)

    def test_solaris_reuses_prepared_python_packages_without_system_pip(self):
        for target in ('solaris','ubuntu'):
            with tempfile.TemporaryDirectory() as temporary, \
                 patch.object(native_ssh,'ROOT',Path(temporary)), \
                 patch.object(native_ssh.sys,'argv',['native_ssh','--target',target]), \
                 patch.object(native_ssh.os,'geteuid',return_value=100,create=True), \
                 patch.object(native_ssh.shutil,'which',return_value='/synthetic/tool'), \
                 patch.object(native_ssh,'private_solaris_dependency',return_value=None), \
                 patch.object(native_ssh,'build_command',return_value=['make','runtime']), \
                 patch('native_remote.verify_os'), \
                 patch.object(native_ssh.subprocess,'run') as run:
                native_ssh.main()
                commands=[call.args[0] for call in run.call_args_list]
                venv=commands[1]
                self.assertEqual('--system-site-packages' in venv,target=='solaris')
                pip=commands[2]
                self.assertEqual(pip[0],str(Path(temporary)/'.native-ssh-venv/bin/python'))
                self.assertEqual(pip[1:4],['-m','pip','install'])
                self.assertEqual(pip[-2:], ['-r', str(Path(temporary)/'tests/ssh-requirements.txt')])
                self.assertIn('tests/test_ssh_transport.py',commands[3])

    def test_missing_libssh_fails_without_installing_or_skipping(self):
        for executable, code in ((None, 0), ('pkg-config', 1)):
            with patch.object(native_ssh.shutil, 'which', return_value=executable), \
                 patch.object(native_ssh.subprocess, 'run', return_value=Mock(returncode=code)) as call:
                with self.assertRaisesRegex(RuntimeError, 'libssh'):
                    native_ssh.build_command('rocky')
                if executable:
                    self.assertEqual(call.call_args.args[0], ['pkg-config', '--atleast-version=0.10', 'libssh'])
                else:
                    call.assert_not_called()

    def test_native_build_uses_libssh_flags_and_keeps_default_disabled(self):
        with patch.object(native_ssh.shutil, 'which', return_value='pkg-config'), \
             patch.object(native_ssh.subprocess, 'run', return_value=Mock(returncode=0)), \
             patch.object(native_ssh.subprocess, 'check_output', side_effect=['-I/fixture/include\n', '-L/fixture/lib -lssh\n']):
            command = native_ssh.build_command('freebsd')
        self.assertIn('SSH_CFLAGS=-DLINER_SSH -I/fixture/include', command)
        self.assertIn('SSH_LIBS=-L/fixture/lib -lssh', command)
        self.assertNotIn('check', command)


if __name__ == '__main__':
    unittest.main()
