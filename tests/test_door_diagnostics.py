"""Compile the diagnostic helper; Unix also exercises the real isolation client."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Diagnostics(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix='ln-diag-')
        cls.addClassCleanup(cls.directory.cleanup)
        cls.base = Path(cls.directory.name)
        cls.compiler = shlex.split(os.environ.get('CC', 'cc'))
        cls.probe = cls.compile('probe', '''
#include "door_diagnostics.h"
int main(void) {
    errno=EDOM;
    door_launch_diagnostic("isolation-connect",42);
    return errno==EDOM?0:1;
}
''')
        cls.reply = cls.compile('reply', '''
#include "door_diagnostics.h"
int main(int argc,char **argv) {
    if(argc!=4)return 2;
    long length=strtol(argv[2],NULL,10);
    errno=EDOM;
    door_reply_diagnostic(length<=0?NULL:argv[1],length,atoi(argv[3]),1,length<0?42:0);
    return errno==EDOM?0:1;
}
''')

    @classmethod
    def compile(cls, name, source, libraries=()):
        path = cls.base/(name+'.c'); path.write_text(source, encoding='utf8')
        binary = cls.base/(name+('.exe' if os.name == 'nt' else ''))
        subprocess.run([*cls.compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(ROOT/'src'), str(path), *libraries, '-o', str(binary)], check=True)
        return binary

    def execute(self, binary, enabled, *args):
        env = os.environ.copy(); env.pop('LINER_DOOR_DIAGNOSTICS', None)
        if enabled is not None: env['LINER_DOOR_DIAGNOSTICS'] = enabled
        return subprocess.run([str(binary), *map(str,args)], env=env, capture_output=True,
                              text=True, timeout=5, check=True)

    def test_default_and_other_values_are_silent_and_preserve_errno(self):
        for enabled in (None, '', '0', 'true', '1\n'):
            with self.subTest(enabled=enabled):
                result = self.execute(self.probe, enabled)
                self.assertEqual(result.stdout+result.stderr, '')

    def test_opt_in_emits_only_static_phase_and_errno(self):
        result = self.execute(self.probe, '1')
        self.assertEqual(result.stdout, '')
        self.assertEqual(result.stderr, 'DOOR_LAUNCH_FAILURE phase=isolation-connect errno=42\n')

    def test_reply_categories_never_print_received_payload(self):
        for value,length,kind in [('OK\n',3,'OK'),('ERR',3,'ERR_PREFIX'),('DON',3,'DONE_PREFIX'),
                                  ('private-ticket',3,'OTHER'),('ok\n',3,'OTHER'),
                                  ('OK',2,'SHORT'),('E',1,'SHORT'),('',0,'EOF'),('',-1,'IO_ERROR')]:
            with self.subTest(kind=kind,value=value):
                result=self.execute(self.reply,'1',value,length,2)
                self.assertEqual(result.stdout,'')
                self.assertEqual(result.stderr,f'DOOR_ISOLATION_REPLY result={kind} bytes={length} fds=2 truncated=1 errno={42 if length<0 else 0}\n')
                self.assertNotIn('private',result.stderr)

    def test_reply_diagnostics_require_exact_opt_in(self):
        for enabled in (None,'','0','true'):
            with self.subTest(enabled=enabled):
                self.assertEqual(self.execute(self.reply,enabled,'private-ticket',3,0).stderr,'')

    @unittest.skipUnless(sys.platform.startswith(('linux','freebsd')), 'Unix isolation client only')
    def test_handoff_wait_has_its_own_bounded_receive_timeout(self):
        binary = self.compile('door-timeouts', '''
#define _GNU_SOURCE
#ifdef __FreeBSD__
#define __BSD_VISIBLE 1
#endif
#include "door_service_client.inc"

static int check_timeout(int fd, int option, long seconds) {
    struct timeval value={0};
    socklen_t size=sizeof(value);
    return getsockopt(fd,SOL_SOCKET,option,&value,&size)==0 &&
           size==sizeof(value) && value.tv_sec==seconds && value.tv_usec==0;
}

int main(void) {
    (void)door_service_start;
    int sockets[2];
    if(socketpair(AF_UNIX,SOCK_STREAM,0,sockets))return 1;
    if(door_service_deadlines(sockets[0]))return 2;
    if(!check_timeout(sockets[0],SO_SNDTIMEO,3))return 3;
    if(!check_timeout(sockets[0],SO_RCVTIMEO,10))return 4;
    close(sockets[0]);
    close(sockets[1]);
    return door_service_deadlines(-1)==-1 && errno==EBADF?0:5;
}
''', ('-lcrypto',))
        self.assertEqual(self.execute(binary, None).stderr, '')

    @unittest.skipUnless(sys.platform.startswith(('linux','freebsd')), 'Unix isolation client only')
    def test_real_isolation_connect_failure_is_private_and_default_silent(self):
        binary = self.compile('isolation', '''
#define _GNU_SOURCE
#ifdef __FreeBSD__
#define __BSD_VISIBLE 1
#endif
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include "door_diagnostics.h"
typedef struct { unsigned id,seconds; } DoorSpec;
typedef struct { int fd,remote,lock; uint64_t until; } Door;
#include "door_isolation.inc"
int main(int argc,char **argv) {
    DoorSpec spec={1,30}; Door done={0,-1,0,0};
    (void)isolated_done(&done);
    if(argc!=2 || !doors_isolation_config(argv[1]) || !isolation_enabled())return 2;
    return isolated_start(&spec,0,123,"private-member",80,24,0,1,1)?3:0;
}
''', ('-lcrypto',))
        endpoint = self.base/'private-socket'
        result = self.execute(binary, '1', endpoint)
        self.assertRegex(result.stderr, r'^DOOR_LAUNCH_FAILURE phase=isolation-connect errno=[0-9]+\n$')
        self.assertNotIn(str(endpoint), result.stderr)
        self.assertNotIn('private-member', result.stderr)
        self.assertEqual(self.execute(binary, None, endpoint).stderr, '')


if __name__ == '__main__':
    unittest.main()
