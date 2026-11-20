"""Service preflight is read-only; readiness verifies a live local peer."""
import os
import json
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock,patch
import door_rc_process as rc_process

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import door_broker as broker
if os.name=='posix':import door_isolation as isolation


class Readiness(unittest.TestCase):
    def test_missing_wrong_owner_regular_file_and_symlink_do_not_connect(self):
        for mode,uid in ((stat.S_IFREG,30),(stat.S_IFLNK,30),(stat.S_IFSOCK,40)):
            with patch.object(Path,'lstat',return_value=Mock(st_mode=mode,st_uid=uid)),patch.object(broker,'socket') as sockets:
                with self.assertRaises(ValueError):broker.check_ready('/fixture/socket',30)
                sockets.socket.assert_not_called()
        with patch.object(Path,'lstat',side_effect=FileNotFoundError),patch.object(broker,'socket') as sockets:
            with self.assertRaises(FileNotFoundError):broker.check_ready('/fixture/socket',30)
            sockets.socket.assert_not_called()

    def probe(self,peer,error=None):
        connection=Mock();connection.__enter__=Mock(return_value=connection);connection.__exit__=Mock(return_value=False)
        connection.connect.side_effect=error
        with patch.object(Path,'lstat',return_value=Mock(st_mode=stat.S_IFSOCK,st_uid=30)),patch.object(broker,'socket',Mock(socket=Mock(return_value=connection))),patch.object(broker,'peer_uid',return_value=peer):
            broker.check_ready('/fixture/socket',30)
        connection.sendall.assert_not_called();connection.send.assert_not_called()
        connection.__exit__.assert_called_once()

    def test_live_peer_checked_without_authentication_request(self):self.probe(30)

    def test_orphan_and_wrong_peer_are_not_ready(self):
        with self.assertRaises(ConnectionRefusedError):self.probe(30,ConnectionRefusedError())
        with self.assertRaises(ValueError):self.probe(40)

    def test_broker_config_check_never_starts_server(self):
        with patch.object(sys,'argv',['broker','--config','/fixture/policy','--check-config']),patch.dict(sys.modules,{'resource':Mock()}),patch.object(broker,'configuration',return_value={}) as config,patch.object(broker,'serve') as serve,patch.object(broker,'check_ready') as ready:
            broker.main()
            config.assert_called_once_with('/fixture/policy');serve.assert_not_called();ready.assert_not_called()

    def test_invalid_broker_config_returns_failure(self):
        with patch.object(sys,'argv',['broker','--config','/fixture/policy','--check-config']),patch.dict(sys.modules,{'resource':Mock()}),patch.object(broker,'configuration',side_effect=ValueError('secret')),patch('sys.stderr') as stderr:
            with self.assertRaises(SystemExit) as caught:broker.main()
            self.assertEqual(caught.exception.code,1)
            self.assertNotIn('secret',str(stderr.write.call_args_list))

    @unittest.skipUnless(os.name=='posix','Unix launcher and shell')
    def test_isolation_preflight_does_not_bind_or_fork(self):
        with patch.object(sys,'argv',['isolation','--config','/fixture/policy','--check-config']),patch.object(isolation,'supported',return_value=True),patch.object(isolation.os,'getuid',return_value=0),patch.object(isolation.resource,'setrlimit'),patch.object(isolation,'config_read',return_value={'socket':'/fixture/socket'}),patch.object(isolation.socket,'socket') as sock,patch.object(isolation.os,'fork') as fork:
            isolation.main();sock.assert_not_called();fork.assert_not_called()

    @unittest.skipUnless(os.name=='posix','shell syntax')
    def test_rc_template_syntax(self):
        for role in ('broker','isolation'):
            subprocess.run(['sh','-n',str(ROOT/'ports/comms/liner-notes/files'/('liner_notes_door_'+role+'.in'))],check=True)


@unittest.skipUnless(os.name=='posix','Unix policy admission')
class WorkingDirectory(unittest.TestCase):
    def test_policy_checks_owner_and_ancestors_before_start(self):
        slot=dict(door=1,node=1,uid=50,gid=50,exe='/fixed/game',cwd='/slots/50',
                  level=0,seconds=30,drop=True,auth=True)
        config=dict(bbs_uid=20,socket='/run/isolation/socket',socket_gid=20,
                    broker_config='/fixed/broker',state_dir='/state',slots=[slot])
        policy=dict(bbs_uid=20,broker_uid=30,socket='/run/broker/socket',door_uids=[50],consumers={40:[1]})
        source=Mock();source.read_text.return_value=json.dumps(config)
        calls=[]
        def secure(path,owner,directory=False):
            calls.append((str(path),owner,directory))
            if str(path)==slot['cwd']:raise ValueError('unsafe ownership or permissions')
            return source
        with patch.object(isolation,'secure_path',side_effect=secure),patch.object(isolation,'broker_configuration',return_value=policy),patch.object(isolation.os,'access',return_value=True):
            with self.assertRaisesRegex(ValueError,'unsafe ownership'):isolation.config_read('/fixed/config')
        self.assertIn(('/slots/50',50,True),calls)

    def test_directory_symlink_writable_parent_and_foreign_owner_are_rejected(self):
        # Virtual stat tree gives deterministic ownership tests without chown.
        for bad in ('symlink','parent-write','owner'):
            def stat_path(path):
                target=str(path)
                mode=stat.S_IFDIR|0o755;uid=0
                if target=='/slots/50':
                    uid=60 if bad=='owner' else 50
                    if bad=='symlink':mode=stat.S_IFLNK|0o777
                if target=='/slots' and bad=='parent-write':mode|=0o020
                return Mock(st_mode=mode,st_uid=uid)
            with patch.object(Path,'lstat',autospec=True,side_effect=stat_path):
                with self.assertRaisesRegex(ValueError,'unsafe ownership'):broker.secure_path('/slots/50',50,True)

class RcLifecycle(unittest.TestCase):
    def process(self,base):
        # No rc command or real process is started by these portable tests.
        process=rc_process.RcProcess.__new__(rc_process.RcProcess)
        process.name='fixture';process.rc=base/'rc'
        process.pidfile=base/'fixture.pid';process.env={}
        process.pid=100;process.returncode=None;process.stopper=None
        process.wait_verified=False
        return process

    def test_old_wait_does_not_inspect_or_stop_replacement(self):
        with tempfile.TemporaryDirectory() as tmp:
            base=Path(tmp);old=self.process(base)
            with patch.object(rc_process.os,'kill',side_effect=ProcessLookupError) as kill, \
                    patch.object(rc_process.subprocess,'run',return_value=Mock(returncode=1)) as run, \
                    patch.object(rc_process.subprocess,'Popen') as spawn:
                self.assertEqual(old.wait(timeout=0),0)
                self.assertEqual(run.call_count,1)
                # A later generation now owns the same rc service and PID file.
                old.pidfile.write_text('200\n')
                run.return_value.returncode=0
                kill.side_effect=None
                self.assertEqual(old.wait(timeout=0),0)
                self.assertEqual(old.poll(),0)
                self.assertEqual(run.call_count,1)
                self.assertEqual(kill.call_count,1)
                spawn.assert_not_called()
                current=self.process(base);current.pid=200
                self.assertIsNone(current.poll())
                with self.assertRaises(subprocess.TimeoutExpired):current.wait(timeout=0)

    def test_first_wait_still_rejects_ready_service(self):
        with tempfile.TemporaryDirectory() as tmp:
            process=self.process(Path(tmp))
            with patch.object(rc_process.os,'kill',side_effect=ProcessLookupError), \
                    patch.object(rc_process.subprocess,'run',return_value=Mock(returncode=0)) as run:
                with self.assertRaisesRegex(AssertionError,'still ready'):process.wait(timeout=0)
                self.assertFalse(process.wait_verified)
                run.return_value.returncode=1
                self.assertEqual(process.wait(timeout=0),0)
                self.assertEqual(run.call_count,2)

    def test_stop_failure_is_not_cached_as_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            process=self.process(Path(tmp));process.stopper=Mock(returncode=1)
            process.stopper.communicate.return_value=(b'',b'stop failed')
            with patch.object(rc_process.os,'kill',side_effect=ProcessLookupError), \
                    patch.object(rc_process.subprocess,'run') as run:
                with self.assertRaisesRegex(AssertionError,'rc stop failed'):process.wait(timeout=0)
                self.assertFalse(process.wait_verified);run.assert_not_called()

    def test_dead_child_waits_for_pidfile_release(self):
        with tempfile.TemporaryDirectory() as tmp:
            process=self.process(Path(tmp));process.pidfile.write_text('100\n')
            with patch.object(rc_process.os,'kill',side_effect=ProcessLookupError), \
                    patch.object(rc_process.subprocess,'run',return_value=Mock(returncode=1)) as run:
                with self.assertRaises(subprocess.TimeoutExpired):process.wait(timeout=0)
                self.assertFalse(process.wait_verified);run.assert_not_called()
                process.pidfile.unlink()
                self.assertEqual(process.wait(timeout=0),0)


if __name__=='__main__':unittest.main()
