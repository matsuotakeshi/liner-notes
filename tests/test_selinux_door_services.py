"""Portable tests for the native adapter, not claims of Enforcing execution."""
import importlib.util
from pathlib import Path
import subprocess
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


if __name__=='__main__':unittest.main()
