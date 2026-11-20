"""Bounded Door admission; fault injection never exhausts the host itself."""
import errno
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock,patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
if os.name=='posix':
    import door_isolation as isolation


@unittest.skipUnless(os.name=='posix','Unix isolation supervisor')
class Admission(unittest.TestCase):
    def test_trickled_input_does_not_extend_deadline(self):
        sock=Mock();sock.recv.return_value=b' '
        with patch.object(isolation.time,'monotonic',side_effect=[0,0,1,2,3]):
            with self.assertRaises(TimeoutError):isolation.read_line(sock)
        self.assertEqual(sock.recv.call_count,3)
        self.assertEqual([c.args[0] for c in sock.settimeout.call_args_list],[3,2,1])

    def test_complete_request_before_deadline(self):
        sock=Mock();sock.recv.side_effect=[b'{',b'}',b'\n']
        with patch.object(isolation.time,'monotonic',return_value=0):
            self.assertEqual(isolation.read_line(sock),{})

    def test_partial_eof_and_oversize_are_rejected(self):
        for data in ([b'{',b''],[b'x']*4097):
            sock=Mock();sock.recv.side_effect=data
            with self.assertRaises(isolation.Rejected):isolation.read_line(sock)

    def test_parent_sequences_are_monotonic_across_failures_and_shared_by_slots(self):
        config={'bbs_uid':10,'slots':[{},{}]};children=set()
        with patch.object(isolation,'peer_uid',return_value=10), \
             patch.object(isolation.os,'fork',side_effect=[101,OSError(errno.EAGAIN,'retry'),102]), \
             patch('sys.stderr'):
            isolation.start_worker(config,Mock(),Mock(),3,4,children)
            incarnation=config['_incarnation']
            self.assertEqual(config['_launch_sequence'],1)
            isolation.start_worker(config,Mock(),Mock(),3,4,children)
            isolation.start_worker(config,Mock(),Mock(),3,4,children)
        self.assertEqual(config['_incarnation'],incarnation)
        self.assertEqual(config['_launch_sequence'],3)
        self.assertEqual(children,{101,102})

    def test_broker_request_uses_only_parent_owned_replay_fields(self):
        endpoint=Mock();endpoint.recv=Mock()
        config={'broker_socket':'/synthetic','broker_uid':30,
                '_incarnation':'trusted-boot','_launch_sequence':9}
        request={'user':1,'door':2,'session':'bbs-session','generation':0,
                 'scope':'forged','sequence':999}
        with patch.object(isolation.socket,'socket',return_value=endpoint), \
             patch.object(isolation,'peer_uid',return_value=30), \
             patch.object(isolation,'read_line',return_value={'ok':True,'result':{'ticket':'a'*64}}), \
             patch.object(isolation,'send') as send:
            isolation.broker_open(config,request,{'uid':50})
        self.assertEqual(send.call_args.args[1],dict(op='open_scoped',member=1,door=2,
                         session='bbs-session',generation=0,scope='trusted-boot.50',sequence=9))

    def test_fork_failure_preserves_existing_workers_then_recovers(self):
        config={'bbs_uid':10,'slots':[{}]};children={101};connection=Mock();listener=Mock()
        for code in (errno.EAGAIN,errno.ENOMEM):
            with self.subTest(errno=code),patch.object(isolation,'peer_uid',return_value=10),patch.object(isolation.os,'fork',side_effect=[OSError(code,'secret'),102]),patch.object(isolation.os,'close') as close,patch('sys.stderr') as stderr:
                isolation.start_worker(config,connection,listener,3,4,children)
                self.assertEqual(children,{101});close.assert_not_called();listener.close.assert_not_called()
                self.assertNotIn('secret',str(stderr.write.call_args_list))
                isolation.start_worker(config,connection,listener,3,4,children)
                self.assertEqual(children,{101,102})
                children.remove(102)

    def test_identity_error_and_worker_capacity_do_not_fork(self):
        config={'bbs_uid':10,'slots':[{}]}
        for peer,children in ((20,set()),(OSError('credentials'),set()),(10,set(range(9)))):
            kwargs={'side_effect':peer} if isinstance(peer,Exception) else {'return_value':peer}
            with patch.object(isolation,'peer_uid',**kwargs),patch.object(isolation.os,'fork') as fork,patch('sys.stderr'):
                before=children.copy()
                isolation.start_worker(config,Mock(),Mock(),3,4,children)
                self.assertEqual(children,before);fork.assert_not_called()

    def test_accept_resource_failures_back_off_then_recover(self):
        for code in (errno.EMFILE,errno.ENFILE,errno.ENOBUFS,errno.ENOMEM):
            listener=Mock();connection=Mock()
            listener.accept.side_effect=[OSError(code,'unavailable'),(connection,None)]
            with patch.object(isolation.time,'sleep') as sleep:
                self.assertIsNone(isolation.accept_connection(listener))
                sleep.assert_called_once_with(.1)
                self.assertIs(isolation.accept_connection(listener),connection)
            listener.close.assert_not_called()

    def test_unknown_accept_error_is_not_hidden(self):
        listener=Mock();listener.accept.side_effect=OSError(errno.EBADF,'invalid listener')
        with self.assertRaises(OSError):isolation.accept_connection(listener)


if __name__=='__main__':unittest.main()
