"""Portable tests for the native adapter, not claims of Enforcing execution."""
import errno
import importlib.util
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import Mock, patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('services',ROOT/'ci/selinux_door_services.py')
services=importlib.util.module_from_spec(spec);spec.loader.exec_module(services)


class Units(unittest.TestCase):
    def test_native_vm_refuses_existing_inactive_identity(self):
        identity=Mock(BBS=20,BROKER=30,GAME=40,SLOT1=50,SLOT2=51)
        pwd=Mock();pwd.getpwuid.return_value=object()
        grp=Mock()
        with patch.dict(sys.modules,{'pwd':pwd,'grp':grp,'test_door_broker_linux':identity}):
            fixture=Mock()
            with self.assertRaisesRegex(ValueError,'already allocated'):services.Backend(fixture,Mock())
            fixture.save_record.assert_not_called()

    def test_native_vm_records_only_unused_slot_ids_for_recovery(self):
        identity=Mock(BBS=20,BROKER=30,GAME=40,SLOT1=50,SLOT2=51)
        pwd=Mock();pwd.getpwuid.side_effect=KeyError
        grp=Mock();grp.getgrgid.side_effect=KeyError
        with patch.dict(sys.modules,{'pwd':pwd,'grp':grp,'test_door_broker_linux':identity}):
            fixture=Mock()
            services.Backend(fixture,Mock())
            self.assertEqual(fixture.door_slot_uids,[50,51])
            self.assertEqual(pwd.getpwuid.call_count,5);self.assertEqual(grp.getgrgid.call_count,5)
            fixture.save_record.assert_called_once()

    def process(self):
        p=services.Process.__new__(services.Process)
        p.backend=Mock();p.name='fixture-generation-1';p.returncode=None;p.stopper=None
        return p

    def test_argv_does_not_expand_environment_or_unit_specifiers(self):
        self.assertEqual(services.argv(['/fixed/app','literal $PATH %i','quote"']),
                         '"/fixed/app" "literal $$PATH %%i" "quote\\\""')

    def test_failed_unit_is_not_success_and_repeated_wait_is_cached(self):
        p=self.process()
        p.backend.command.return_value.stdout='MainPID=0\nActiveState=failed\nExecMainStatus=9\n'
        self.assertEqual(p.wait(0),9)
        p.backend.command.reset_mock()
        self.assertEqual(p.wait(0),9);p.backend.command.assert_not_called()

    def test_live_deactivating_unit_is_not_considered_stopped(self):
        p=self.process()
        p.backend.command.return_value.stdout='MainPID=200\nActiveState=deactivating\nExecMainStatus=0\n'
        with self.assertRaises(subprocess.TimeoutExpired):p.wait(0)
        self.assertIsNone(p.returncode)

    def test_failed_stop_is_reported(self):
        p=self.process();p.returncode=0;p.stopper=Mock(returncode=1)
        p.stopper.communicate.return_value=(None,b'failure')
        with self.assertRaisesRegex(AssertionError,'systemd stop'):p.wait(0)

    def test_kill_targets_only_unique_main_process(self):
        p=self.process();p.kill()
        p.backend.command.assert_called_once_with('systemctl','kill','--kill-whom=main','--signal=KILL',p.name)

    def test_service_mapping_preserves_hardening_and_never_resets_state(self):
        fixture=Mock(path=Path('/var/lib/fixture'))
        backend=services.Backend.__new__(services.Backend)
        backend.fixture=fixture;backend.command=Mock()
        backend.base=Path('/var/lib/fixture/test')
        backend.configs={'broker':Path('/fixture/broker'),'isolation':Path('/fixture/isolation')}
        backend.ids={'broker':30,'isolation':0,'consumer':40}
        # readiness's import is local to the native path; provide its public API.
        templates={r:(ROOT/'linux'/('liner-notes-door-'+r+'.service')).read_text() for r in backend.configs}
        with patch.dict(sys.modules,{'door_broker':Mock()}),patch.object(services.Path,'read_text',autospec=True) as read,patch.object(services,'Process') as proc:
            proc.return_value.poll.return_value = None
            read.side_effect=lambda path: ('{"socket":"/fixture/socket"}' if path in backend.configs.values()
                                          else templates['broker' if 'broker' in path.name else 'isolation'])
            for role in templates:
                backend.start_service(role)
                text=proc.call_args.args[2]
                for line in ('Restart=no','NoNewPrivileges=yes','ProtectSystem=strict','ProtectHome=yes'):
                    self.assertIn(line,text)
                self.assertIn('--check-config',text)
                self.assertNotIn('ExecStop=',text);self.assertNotIn('ExecStartPost=',text)
                if role=='isolation':
                    for line in ('KillMode=process','SendSIGKILL=no','TimeoutStopSec=infinity'):
                        self.assertIn(line,text)

    def test_launch_diagnostics_are_enabled_only_in_fixture_bbs_unit(self):
        backend = services.Backend.__new__(services.Backend)
        backend.base = Path('/fixture'); backend.ids = {'bbs':20, 'consumer':40}
        with patch.object(services, 'Process') as process:
            for role, domain in [('bbs','liner_notes_t'), ('consumer','liner_notes_consumer_t')]:
                backend.unit(role, ['/fixed/entry'], domain)
                text = process.call_args.args[2]
                self.assertEqual('Environment=LINER_DOOR_DIAGNOSTICS=1' in text, role == 'bbs')


class SocketReadiness(unittest.TestCase):
    def setUp(self):
        self.process = Mock(); self.process.poll.return_value = None
        self.check = Mock()
        modules = patch.dict(sys.modules, {'door_broker': Mock(check_ready=self.check)})
        modules.start(); self.addCleanup(modules.stop)
        sleep = patch.object(services.time, 'sleep')
        self.sleep = sleep.start(); self.addCleanup(sleep.stop)

    def test_missing_then_bound_not_listening_then_ready(self):
        self.check.side_effect = [FileNotFoundError(errno.ENOENT, 'not created'),
                                 ConnectionRefusedError(errno.ECONNREFUSED, 'not listening'), None]
        services.wait_ready(self.process, '/fixture/socket', 30)
        self.assertEqual(self.check.call_count, 3)
        for call in self.check.call_args_list: self.assertEqual(call.args, ('/fixture/socket', 30))
        self.assertEqual(self.sleep.call_count, 2)
        self.process.diagnostics.assert_not_called()

    def test_absent_socket_stays_failure_at_deadline(self):
        self.check.side_effect = FileNotFoundError(errno.ENOENT, 'absent')
        with self.assertRaisesRegex(TimeoutError, 'deadline expired'):
            services.wait_ready(self.process, '/fixture/socket', 30, timeout=0)
        self.process.diagnostics.assert_called_once()
        self.sleep.assert_not_called()

    def test_early_exit_is_not_retried(self):
        self.process.poll.return_value = 1
        with self.assertRaisesRegex(RuntimeError, 'exited before'):
            services.wait_ready(self.process, '/fixture/socket', 30)
        self.check.assert_not_called(); self.sleep.assert_not_called()
        self.process.diagnostics.assert_called_once()

    def test_bad_identity_and_permission_failure_are_not_retried(self):
        for error in (ValueError('socket peer identity'), ValueError('socket identity'),
                      PermissionError(errno.EACCES, 'denied')):
            self.check.reset_mock(); self.process.diagnostics.reset_mock()
            self.check.side_effect = error
            with self.assertRaises(type(error)):
                services.wait_ready(self.process, '/fixture/socket', 30)
            self.check.assert_called_once(); self.sleep.assert_not_called()
            self.process.diagnostics.assert_called_once()


class FixtureAccounts(unittest.TestCase):
    def setUp(self):
        self.users, self.groups, self.calls = {}, {}, []
        def lookup(data, key, field):
            for value in data.values():
                if getattr(value, field) == key: return value
            raise KeyError(key)
        self.pwd = Mock()
        self.pwd.getpwnam.side_effect = lambda key: lookup(self.users, key, 'pw_name')
        self.pwd.getpwuid.side_effect = lambda key: lookup(self.users, key, 'pw_uid')
        self.pwd.getpwall.side_effect = lambda: list(self.users.values())
        self.grp = Mock()
        self.grp.getgrnam.side_effect = lambda key: lookup(self.groups, key, 'gr_name')
        self.grp.getgrgid.side_effect = lambda key: lookup(self.groups, key, 'gr_gid')
        with patch.dict(sys.modules, {'pwd': self.pwd, 'grp': self.grp}):
            self.accounts = services.Accounts(Mock(path=Path('/fixture/generation')), self.command,
                                              30, 40, (20, 30, 40, 50, 51), bbs=20)
        self.idle = patch.object(self.accounts, 'idle'); self.idle.start(); self.addCleanup(self.idle.stop)

    def command(self, *args):
        self.calls.append(args)
        name = args[-1]
        if args[0].endswith('/groupadd'):
            self.groups[name] = SimpleNamespace(gr_name=name, gr_gid=int(args[2]), gr_mem=[])
        elif args[0].endswith('/useradd'):
            self.users[name] = SimpleNamespace(pw_name=name, pw_uid=int(args[args.index('--uid')+1]),
                                               pw_gid=int(args[args.index('--gid')+1]),
                                               pw_dir='/nonexistent', pw_shell='/usr/sbin/nologin')
        elif args[0].endswith('/userdel'): del self.users[name]
        elif args[0].endswith('/groupdel'): del self.groups[name]

    def test_create_nonlogin_identity_and_remove_only_owned_entries(self):
        self.accounts.create()
        self.assertEqual(self.pwd.getpwuid(30).pw_name, self.accounts.user)
        for uid in (20, 30, 40):
            self.assertEqual(self.pwd.getpwuid(uid).pw_gid, uid)
            self.assertEqual(self.grp.getgrgid(uid).gr_mem, [])
        for uid in (50, 51):
            with self.assertRaises(KeyError): self.pwd.getpwuid(uid)
        for call in self.calls[-3:]:
            for flag in ('--no-create-home', '--no-log-init', '--no-user-group'): self.assertIn(flag, call)
            self.assertEqual(call[call.index('--password')+1], '!')
        self.accounts.close()
        self.assertFalse(self.users); self.assertFalse(self.groups)
        self.assertEqual([c[0] for c in self.calls[-6:]],
                         ['/usr/sbin/userdel']*3 + ['/usr/sbin/groupdel']*3)
        for c in self.calls[-6:]: self.assertEqual(len(c), 2)
        self.assertEqual(self.accounts.fixture.door_accounts, [])

    def test_collision_causes_no_changes(self):
        self.groups['foreign'] = SimpleNamespace(gr_name='foreign', gr_gid=40, gr_mem=[])
        with self.assertRaisesRegex(ValueError, 'already allocated'): self.accounts.create()
        self.assertFalse(self.calls)

    def test_name_collision_causes_no_changes(self):
        name = self.accounts.groups[0][0]
        self.groups[name] = SimpleNamespace(gr_name=name, gr_gid=1234, gr_mem=[])
        with self.assertRaisesRegex(ValueError, 'already allocated'): self.accounts.create()
        self.assertFalse(self.calls)

    def test_partial_creation_is_recoverable(self):
        original = self.accounts.command
        def fail(*args):
            if args[0].endswith('/useradd'): raise RuntimeError('creation failed')
            return original(*args)
        self.accounts.command = fail
        with self.assertRaisesRegex(RuntimeError, 'creation failed'): self.accounts.create()
        self.assertEqual(len(self.accounts.fixture.door_accounts), 3)
        self.accounts.close()
        self.assertFalse(self.groups)

    def test_partial_user_creation_removes_only_successful_accounts(self):
        original = self.accounts.command
        def fail(*args):
            if args[0].endswith('/useradd') and args[-1] == self.accounts.users[1][0]:
                raise RuntimeError('consumer creation failed')
            return original(*args)
        self.accounts.command = fail
        with self.assertRaisesRegex(RuntimeError, 'consumer creation failed'): self.accounts.create()
        self.assertEqual(len(self.accounts.fixture.door_account_plan), 6)
        self.assertEqual(len(self.accounts.fixture.door_accounts), 4)
        self.accounts.close()
        self.assertFalse(self.users); self.assertFalse(self.groups)

    def test_consumer_and_bbs_name_collisions_are_checked_before_changes(self):
        for name, uid in self.accounts.users[1:]:
            with self.subTest(uid=uid):
                self.users[name] = SimpleNamespace(pw_name=name, pw_uid=999)
                with self.assertRaisesRegex(ValueError, 'already allocated'): self.accounts.create()
                self.assertFalse(self.calls)
                del self.users[name]

    def test_changed_user_retained(self):
        self.accounts.create(); self.calls.clear()
        self.users[self.accounts.user].pw_uid = 999
        with self.assertRaisesRegex(ValueError, 'account changed'): self.accounts.close()
        self.assertFalse(self.calls)

    def test_foreign_group_member_retained(self):
        self.accounts.create(); self.calls.clear()
        self.groups[self.accounts.groups[1][0]].gr_mem = ['foreign']
        with self.assertRaisesRegex(ValueError, 'group changed'): self.accounts.close()
        self.assertFalse(self.calls)

    def test_foreign_primary_group_retained(self):
        self.accounts.create(); self.calls.clear()
        self.users['foreign'] = SimpleNamespace(pw_name='foreign', pw_uid=888, pw_gid=40)
        with self.assertRaisesRegex(ValueError, 'Foreign user'): self.accounts.close()
        self.assertFalse(self.calls)

    def test_live_process_prevents_cleanup(self):
        self.accounts.create(); self.calls.clear()
        self.accounts.idle.side_effect = ValueError('Fixture identity still has a live process')
        with self.assertRaisesRegex(ValueError, 'live process'): self.accounts.close()
        self.assertFalse(self.calls)

    def test_proc_scan_includes_saved_uid_and_supplementary_groups(self):
        self.idle.stop()
        for line in ('Uid: 0 0 30 0', 'Gid: 0 40 0 0', 'Groups: 1 51'):
            path = Mock(); path.read_text.return_value = line
            with patch.object(services.Path, 'glob', return_value=[path]):
                with self.assertRaisesRegex(ValueError, 'live process'): self.accounts.idle()


class Diagnostics(unittest.TestCase):
    def test_start_failure_records_journal_and_preserves_original_error(self):
        backend = Mock(); backend.fixture.path = Path('/fixture/test'); backend.units = []
        backend.fixture.units = []
        def command(*args, **kw):
            if args[:2] == ('systemctl', 'show'): return Mock(stdout='not-found')
            if args[:2] == ('systemctl', 'start'): raise RuntimeError('original startup failure')
            if args[0] == 'journalctl': return Mock(stdout='status=217/USER', returncode=0)
            return Mock(stdout='')
        backend.command.side_effect = command
        from unittest.mock import mock_open
        with patch.object(services.Path, 'open', mock_open()), patch.object(services.Path, 'chmod'), \
             patch('builtins.print') as output:
            with self.assertRaisesRegex(RuntimeError, 'original startup failure'):
                services.Process(backend, 'broker', 'unit', 'liner_notes_broker_t')
            self.assertTrue(any('217/USER' in str(c) for c in output.call_args_list))
        journal = [c for c in backend.command.call_args_list if c.args[0] == 'journalctl']
        self.assertEqual(len(journal), 1)
        self.assertIn('test-door-broker-0', journal[0].args)
        self.assertFalse(journal[0].kwargs['check'])

    def test_diagnostics_are_bounded_and_keep_journal_failure_visible(self):
        p = services.Process.__new__(services.Process); p.name = 'unique-fixture'; p.backend = Mock()
        p.backend.command.return_value = Mock(stdout='あ'*20000, returncode=1)
        with patch('builtins.print') as output: p.diagnostics()
        self.assertLessEqual(len(p.stderr_cache.getvalue()), 16384)
        self.assertIn('unavailable (exit 1)', output.call_args.args[0])

    def test_failed_diagnostics_does_not_mask_start_failure(self):
        p = services.Process.__new__(services.Process); p.name = 'unique-fixture'; p.backend = Mock()
        p.backend.command.side_effect = RuntimeError('journal unavailable')
        with patch('builtins.print') as output: p.diagnostics()
        self.assertIn('unavailable', output.call_args.args[0])


def real_accounts():
    """Only inside the disposable hosted Rocky container, never a host setup."""
    import os, pwd, grp
    if os.environ.get('CI_SELINUX_DISPOSABLE') != '1' or not Path('/.dockerenv').is_file() or os.geteuid() != 0:
        raise RuntimeError('Disposable container required')
    def command(*args): subprocess.run(args, check=True, capture_output=True, text=True)
    with tempfile.TemporaryDirectory(prefix='lnci-accounts-') as directory:
        fixture = Mock(path=Path(directory))
        accounts = services.Accounts(fixture, command, 64502, 64503, range(64501, 64506), bbs=64501)
        try:
            accounts.create()
            for name, uid in accounts.users:
                assert pwd.getpwuid(uid).pw_name == name
                assert grp.getgrgid(uid).gr_mem == []
                assert os.getgrouplist(name, uid) == [uid]
                # Resolve NSS and initialize groups as systemd does for each
                # service; raw numeric setuid alone missed the consumer failure.
                subprocess.run([sys.executable, '-c',
                    'import os, pwd, sys; uid=int(sys.argv[1]); name=pwd.getpwuid(uid).pw_name; '
                    'os.initgroups(name, uid); os.setgid(uid); os.setuid(uid); '
                    'assert os.getuid()==uid and os.getgid()==uid and os.getgroups()==[uid]', str(uid)], check=True)
            subprocess.run([sys.executable, '-c',
                'import os; assert os.getuid()==64502; assert os.getgid()==64502; assert os.getgroups()==[64503]'],
                user=64502, group=64502, extra_groups=[64503], check=True)
        finally:
            accounts.close()
        for lookup, key in ((fn, uid) for uid in range(64501, 64506) for fn in (pwd.getpwuid, grp.getgrgid)):
            try: lookup(key)
            except KeyError: pass
            else: raise AssertionError('Fixture identity survived cleanup')
    print('PASS disposable Rocky fixture NSS identity lifecycle')


if __name__=='__main__':
    if sys.argv[1:] == ['--accounts-in-disposable-container']: real_accounts()
    else: unittest.main()
