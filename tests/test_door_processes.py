"""Portable backend contracts plus own-process native peer/reaper checks."""
import ctypes
import errno
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import door_processes as dp
from door_broker import peer_uid
import door_broker


class Backend(unittest.TestCase):
    def test_freebsd_abi_and_owned_scope_only(self):
        self.assertEqual(ctypes.sizeof(dp.ReaperStatus),80)
        self.assertEqual(ctypes.sizeof(dp.ReaperKill),80)
        calls=[]
        def control(cmd,value=None):
            calls.append(cmd)
            if cmd==4:
                value.flags=1;value.reaper=os.getpid();value.descendants=2
            if cmd==6:
                self.assertEqual(value.flags,0)
                self.assertEqual(value.signal,9)
        with patch.object(dp.sys,'platform','freebsd15'),patch.object(dp,'procctl',side_effect=control):
            scope=dp.Scope(65004)
            self.assertTrue(scope.active());scope.signal(9)
            self.assertEqual(calls,[2,4,6])
            with self.assertRaises(OSError):dp.processes(65004,terminate=True)

    def test_unowned_or_failed_status_is_not_empty(self):
        with patch.object(dp.sys,'platform','freebsd15'),patch.object(dp,'procctl') as control:
            scope=dp.Scope(65004)
            with self.assertRaises(OSError):scope.active()
            control.side_effect=OSError(errno.EPERM,'rejected')
            with self.assertRaises(OSError):scope.signal(9)
            control.side_effect=OSError(errno.ESRCH,'empty')
            scope.signal(9)

    def test_no_new_privileges_requires_confirmation(self):
        with patch.object(dp.sys,'platform','freebsd15'),patch.object(dp,'procctl') as control:
            with self.assertRaises(OSError):dp.no_new_privileges()
            def enable(cmd,value):
                if cmd==20:value.value=1
            control.side_effect=enable
            dp.no_new_privileges()

    def test_solaris_launch_fails_closed(self):
        with patch.object(dp.sys,'platform','sunos5'):
            self.assertFalse(dp.supported())
            with self.assertRaises(OSError):dp.Scope(65004)
            with self.assertRaises(OSError):dp.no_new_privileges()

    def test_solaris_peer_credential_is_freed(self):
        class Call:
            def __init__(self,fn):self.fn=fn
            def __call__(self,*args):return self.fn(*args)
        class Lib:
            def __init__(self):
                self.freed=[]
                def get(fd,out):out._obj.value=123;return 0
                self.getpeerucred=Call(get)
                self.ucred_geteuid=Call(lambda pointer:65002)
                self.ucred_free=Call(lambda pointer:self.freed.append(pointer.value))
        class Sock:
            def fileno(self):return 7
        lib=Lib()
        with patch.object(door_broker.sys,'platform','sunos5'),patch.object(door_broker.ctypes,'CDLL',return_value=lib):
            self.assertEqual(peer_uid(Sock()),65002)
            self.assertEqual(lib.freed,[123])
            lib.ucred_geteuid=Call(lambda pointer:ctypes.c_uint(-1).value)
            with self.assertRaises(OSError):peer_uid(Sock())
            self.assertEqual(lib.freed,[123,123])

    def test_service_templates_keep_cleanup_and_opt_in(self):
        isolation=(ROOT/'linux/liner-notes-door-isolation.service').read_text()
        broker=(ROOT/'linux/liner-notes-door-broker.service').read_text()
        for role,unit in (('isolation',isolation),('broker',broker)):
            self.assertIn('NoNewPrivileges=yes',unit)
            self.assertIn('Restart=no',unit)
            # Only the read-only preflight may run before startup. Keep rejecting
            # reset/cleanup commands that could discard a live Door reservation.
            command=('@PYTHON@ -E -s -B @PREFIX@/share/liner-notes/scripts/'
                     f'door_{role}.py --config /etc/liner-notes/door-{role}.json')
            self.assertEqual([line for line in unit.splitlines() if line.startswith('Exec')],
                             ['ExecStartPre='+command+' --check-config',
                              'ExecStart='+command])
        for line in ('KillMode=process','SendSIGKILL=no','TimeoutStopSec=infinity',
                     'StateDirectory=liner-notes-door-isolation','User=root'):
            self.assertIn(line,isolation)
        self.assertIn('User=liner_notes_broker',broker)
        self.assertIn('RestrictAddressFamilies=AF_UNIX',broker)

    def test_freebsd_uid_scan_failures_and_saved_uid(self):
        with patch.object(dp.sys,'platform','freebsd15'),patch.object(dp.subprocess,'run') as run:
            run.return_value.stdout='  0 0 0\n 22 22 65004\n'
            self.assertTrue(dp.processes(65004));self.assertFalse(dp.processes(65005))
            self.assertEqual(run.call_args.args[0],['/bin/ps','-ax','-o','uid=','-o','ruid=','-o','svuid='])
            # The old combined format yields a header plus only one UID column.
            for invalid in ('','?? 0 0\n','0 0\n',',ruid=,svuid=\n 0\n 65004\n'):
                run.return_value.stdout=invalid
                with self.assertRaises(OSError):dp.processes(65004)

    @unittest.skipUnless(sys.platform.startswith('freebsd'),'native FreeBSD ps syntax')
    def test_native_freebsd_uid_inventory_contains_self(self):
        # Exercise the real ps parser without privileges, not a mocked Linux
        # rendering. Even restricted visibility must include the calling UID.
        self.assertTrue(dp.processes(os.getuid()))
        self.assertTrue(dp.processes(os.geteuid()))

    @unittest.skipUnless(os.name=='posix','native Unix socket credentials')
    def test_native_peer_identity(self):
        with tempfile.TemporaryDirectory(prefix='door-peer-') as tmp:
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as listener:
                path=str(Path(tmp)/'peer');listener.bind(path);listener.listen(1)
                with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as client:
                    client.connect(path)
                    connection,_=listener.accept()
                    with connection:
                        self.assertEqual(peer_uid(connection),os.geteuid())
                        self.assertEqual(peer_uid(client),os.geteuid())

    @unittest.skipUnless(sys.platform.startswith('freebsd'),'native FreeBSD reaper')
    def test_native_freebsd_detached_descendants(self):
        # Isolated worker, same unprivileged CI UID. Never signal the caller's UID
        # or an enumerated PID; procctl targets only the worker's own descendants.
        code=r'''
import os,signal,time
from door_processes import Scope,no_new_privileges
scope=Scope(os.getuid());no_new_privileges()
r,w=os.pipe()
pid=os.fork()
if pid==0:
    os.close(r);os.setsid()
    pid=os.fork()
    if pid:os._exit(0)
    signal.signal(signal.SIGHUP,signal.SIG_IGN)
    os.write(w,b'R');os.close(w)
    while True:time.sleep(1)
os.close(w)
try:
    assert os.read(r,1)==b'R'
    assert scope.active()
finally:
    os.close(r);scope.stop()
assert not scope.active()
'''
        subprocess.run([sys.executable,'-c',code],cwd=ROOT/'scripts',check=True,timeout=15)


if __name__=='__main__':unittest.main()
