"""Shared native host admission, without connecting to any test VM."""
import os,sys,tempfile,threading,time,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'ci'))
from native_remote import host_lock,run_commands

@unittest.skipIf(os.name=='nt','POSIX advisory locks and pipe monitoring')
class SharedLockTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.path=Path(self.tmp.name)/'shared.lock';self.read,self.write=os.pipe()
        self.addCleanup(os.close,self.read);self.addCleanup(lambda:os.close(self.write) if self.write is not None else None)
    def test_waits_for_release_and_preserves_partial_heartbeat(self):
        result=[]
        def waiter():
            with host_lock(self.path,self.read,3,3) as state:result.append(state)
        with host_lock(self.path):
            thread=threading.Thread(target=waiter);thread.start();os.write(self.write,b'al')
            time.sleep(.3);self.assertFalse(result)
        thread.join(3);self.assertFalse(thread.is_alive());self.assertEqual(result[0]['pending'],b'al')
        os.write(self.write,b'ive\n')
        run_commands([[sys.executable,'-c','pass']],self.read,3,3,heartbeat_state=result[0])
    def test_wait_disconnect_cancel_timeout_and_expired_lease(self):
        with host_lock(self.path):
            with self.assertRaisesRegex(RuntimeError,'cancelled'),host_lock(self.path,self.read,1,1,lambda:True):pass
            with self.assertRaisesRegex(RuntimeError,'timed out'),host_lock(self.path,self.read,0,1):pass
            with self.assertRaisesRegex(RuntimeError,'heartbeat expired'),host_lock(self.path,self.read,2,.05):pass
            os.close(self.write);self.write=None
            with self.assertRaisesRegex(RuntimeError,'disconnected'),host_lock(self.path,self.read,2,1):pass
        with host_lock(self.path):pass

if __name__=='__main__':unittest.main(verbosity=2)
