"""Fault-injected worker evidence, without forking or changing host privileges."""
import contextlib
import errno
import io
import json
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
# These Unix modules are not executed by the mocked control-flow tests.
with patch.dict(sys.modules, {name:Mock() for name in ('fcntl','pty','resource')} if os.name=='nt' else {}):
    import door_isolation as isolation


class ExitWorker(BaseException):pass


class Diagnostics(unittest.TestCase):
    def test_unexpected_exception_is_recorded_and_cleanup_still_runs(self):
        connection=Mock();error=RuntimeError('secret-ticket /private/member')
        output=io.StringIO()
        with patch.object(isolation,'read_line',side_effect=error), contextlib.redirect_stderr(output):
            with self.assertRaises(RuntimeError) as caught:isolation.supervise({},connection)
        self.assertIs(caught.exception,error)
        self.assertEqual(json.loads(output.getvalue()),dict(event='launch-failed',phase='request',error='RuntimeError',errno=None))
        connection.sendall.assert_called_once_with(b'DONE\n');connection.close.assert_called_once()

    def test_diagnostic_output_failure_cannot_skip_error_reply_or_cleanup(self):
        connection=Mock();stderr=Mock();stderr.write.side_effect=OSError('journal blocked')
        with patch.object(isolation,'read_line',side_effect=PermissionError(errno.EACCES,'secret')),patch('sys.stderr',stderr):
            isolation.supervise({},connection)
        self.assertEqual([c.args for c in connection.sendall.call_args_list],[(b'ERR\n',),(b'DONE\n',)])
        connection.close.assert_called_once()

    def test_cleanup_failure_preserves_slot_and_revokes_before_cleanup(self):
        connection=Mock();issuer=Mock();scope=Mock();output=io.StringIO()
        def fail_cleanup():
            issuer.close.assert_called_once()
            raise RuntimeError('secret cleanup path')
        scope.stop.side_effect=fail_cleanup
        with patch.object(isolation,'read_line',return_value={'door':1,'level':0}), \
             patch.object(isolation,'request_check',return_value='private-member'), \
             patch.object(isolation,'slot_acquire',return_value=({'uid':10,'auth':True},123)), \
             patch.object(isolation,'Scope',return_value=scope), \
             patch.object(isolation,'broker_open',return_value=(issuer,'secret-ticket')), \
             patch.object(isolation.os,'pipe',side_effect=OSError(errno.EMFILE,'private-path')), \
             patch.object(isolation.os,'ftruncate') as truncate, contextlib.redirect_stderr(output):
            with self.assertRaises(RuntimeError):isolation.supervise({},connection)
        entries=[json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual([(e['event'],e['phase']) for e in entries],[('launch-rejected','ticket-pipe'),('cleanup-failed','scope-stop')])
        truncate.assert_not_called()
        self.assertNotIn('secret',output.getvalue());self.assertNotIn('private',output.getvalue())
        connection.sendall.assert_called_once_with(b'ERR\n')

    def worker(self,error=None,setup_error=False):
        config={'bbs_uid':10,'slots':[{}]};children={99};connection=Mock();listener=Mock()
        if setup_error:listener.close.side_effect=OSError(errno.EBADF,'private-path')
        output=io.StringIO()
        with patch.object(isolation,'peer_uid',return_value=10), \
             patch.object(isolation.os,'fork',return_value=0,create=True), \
             patch.object(isolation.os,'close'),patch.object(isolation.signal,'signal'), \
             patch.object(isolation,'supervise',side_effect=error) as supervise, \
             patch.object(isolation.os,'_exit',side_effect=ExitWorker) as exit_worker,contextlib.redirect_stderr(output):
            with self.assertRaises(ExitWorker):isolation.start_worker(config,connection,listener,3,4,children)
        self.assertEqual(children,{99})
        return exit_worker,supervise,output.getvalue()

    def test_failed_worker_exits_nonzero_without_exception_message(self):
        for error in (RuntimeError('private-path'),SystemExit('secret-ticket')):
            with self.subTest(error=type(error).__name__):
                exit_worker,_,output=self.worker(error)
                exit_worker.assert_called_once_with(1)
                entry=json.loads(output);self.assertEqual(entry['event'],'worker-failed');self.assertEqual(entry['phase'],'supervise')
                self.assertNotIn('private',output);self.assertNotIn('secret',output)

    def test_setup_failure_is_distinguished(self):
        exit_worker,supervise,output=self.worker(setup_error=True)
        exit_worker.assert_called_once_with(1);supervise.assert_not_called()
        self.assertEqual(json.loads(output)['phase'],'setup')

    def test_successful_worker_stays_silent_with_zero_exit(self):
        exit_worker,supervise,output=self.worker()
        exit_worker.assert_called_once_with(0);supervise.assert_called_once();self.assertEqual(output,'')


if __name__=='__main__':unittest.main()
