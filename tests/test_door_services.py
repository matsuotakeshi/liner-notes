"""Service preflight is read-only; readiness verifies a live local peer."""
import os
from pathlib import Path
import stat
import subprocess
import sys
import unittest
from unittest.mock import Mock,patch

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


if __name__=='__main__':unittest.main()
