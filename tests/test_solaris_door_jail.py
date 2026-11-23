"""Portable guard/evidence tests; never substitutes for approved Solaris root run."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shlex
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'ci'))
import solaris_door_jail as fixture


def elf_image(path='/usr/lib/amd64/ld.so.1', count=1):
    payload = path.encode('ascii') + b'\0'
    header = b'\x7fELF\x02\x01\x01'+bytes(9)
    header += struct.pack('<HHIQQQIHHHHHH',2,62,1,0,64,0,0,64,56,count,0,0,0)
    table = b''.join(struct.pack('<IIQQQQQQ',3,4,64+56*count,0,0,len(payload),len(payload),1)
                     for _ in range(count))
    return header+table+payload


def matrix():
    rows = {}
    for name in fixture.CASES:
        before = dict.fromkeys(fixture.ID_FIELDS,61001)
        before.update(groups=0,basic_limit=False)
        if name != 'plain':
            kind, role = name.split('-')
            for field in (('euid','suid') if kind == 'uid' else ('egid','sgid')):
                before[field] = fixture.ROLES[role]
        after = dict.fromkeys(fixture.ID_FIELDS,61001)
        after.update(groups=0,**dict.fromkeys(fixture.FLAGS,True))
        rows[name] = {'baseline':before,'jailed':after}
    return rows


class JailFixture(unittest.TestCase):
    def test_default_plan_does_not_authorize_execution(self):
        result = subprocess.run([sys.executable,str(ROOT/'ci/solaris_door_jail.py')],
                                check=True,capture_output=True,text=True,timeout=10)
        value = json.loads(result.stdout)
        self.assertTrue(value['default_read_only'])
        self.assertTrue(value['requires_separate_mount_approval'])
        self.assertTrue(value['holder_death_requires_explicit_opt_in'])
        self.assertFalse(value['door_backend_enabled'])
        self.assertEqual(value['mount']['options'],'nosuid,size=64m')

    def test_lifecycle_rejects_missing_processes_wrong_contract_and_survivors(self):
        holder, leader, detached, control = 100, 101, 102, 103
        ready = b'READY 101 102\n'
        self.assertEqual(fixture.ready_pids(ready,holder,control),(leader,detached))
        for bad in (b'READY 100 102\n',b'READY 101 101\n',b'READY 101 102',b'READY -1 102\n'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                fixture.ready_pids(bad,holder,control)

        before = {
            holder: dict(status='S',parent=50,session=100,contract=7),
            leader: dict(status='S',parent=100,session=100,contract=8),
            detached: dict(status='S',parent=101,session=102,contract=8),
            control: dict(status='S',parent=50,session=50,contract=7),
        }
        self.assertEqual(fixture.verify_membership(before,holder,leader,detached,control),8)
        wrong_contract = {**before, detached: dict(before[detached],contract=7)}
        missing_child = {pid:row for pid,row in before.items() if pid != detached}
        wrong_session = {**before, holder: dict(before[holder],session=50)}
        for bad in (wrong_contract, missing_child, wrong_session):
            with self.assertRaises(ValueError):
                fixture.verify_membership(bad,holder,leader,detached,control)
        fixture.verify_cleanup({control:before[control]},leader,detached,control,8)
        fixture.verify_cleanup({control:before[control],detached:{'status':'Z'}},leader,detached,control,8)
        for bad in ({}, {control:before[control],detached:before[detached]},
                    {control:dict(before[control],contract=8)}):
            with self.assertRaises(ValueError):
                fixture.verify_cleanup(bad,leader,detached,control,8)

    def test_process_snapshot_accepts_missing_ids_only_for_zombies(self):
        with patch.object(fixture.subprocess,'run') as run:
            run.return_value = subprocess.CompletedProcess([],0,'102 1 - - Z\n103 50 50 7 S\n','')
            self.assertEqual(fixture.contract_rows('/usr/bin/ps',(102,103)),{
                102: {'parent':1,'session':None,'contract':None,'status':'Z'},
                103: {'parent':50,'session':50,'contract':7,'status':'S'}
            })
            run.return_value = subprocess.CompletedProcess([],0,'112 1 102 7 S\n','')
            with self.assertRaises(ValueError):
                fixture.contract_rows('/usr/bin/ps',(102,103))

    def test_prepare_hashes_all_sources_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = fixture.prepare(Path(temporary)/'new','a'*40)
            manifest = json.loads((out/'manifest.json').read_text())
            for name in fixture.SOURCES:
                self.assertEqual(manifest['sha256'][name],hashlib.sha256((out/name).read_bytes()).hexdigest())
            with self.assertRaises(FileExistsError): fixture.prepare(out,'a'*40)
            with self.assertRaises(ValueError): fixture.prepare(Path(temporary)/'bad','main')

    def test_bundle_verified_before_import_or_root_side_effect(self):
        with self.assertRaises(ValueError): fixture.verify_bundle()
        with patch.object(fixture.platform,'system',return_value='Linux'), patch.object(fixture,'verify_bundle') as verify:
            with self.assertRaises(ValueError): fixture.run()
            verify.assert_not_called()
        for uid, euid in ((1000,1000),(1000,0),(0,1000)):
            with patch.object(fixture.platform,'system',return_value='SunOS'), \
                 patch.object(fixture.os,'getuid',return_value=uid,create=True), \
                 patch.object(fixture.os,'geteuid',return_value=euid,create=True), \
                 patch.object(fixture,'verify_bundle') as verify:
                with self.assertRaises(ValueError): fixture.run()
                verify.assert_not_called()

    def test_tampered_bundle_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = fixture.prepare(Path(temporary)/'new','a'*40).resolve()
            with patch.object(fixture,'INSTALL',out), patch.object(fixture,'__file__',str(out/'solaris_door_jail.py')), patch.object(fixture,'trusted'):
                self.assertEqual(fixture.verify_bundle()['source_sha'],'a'*40)
                (out/'solaris_door_boundary.py').write_text('untrusted')
                with self.assertRaises(ValueError): fixture.verify_bundle()

    def test_mount_options_and_nested_mounts_fail_closed(self):
        root = '/var/tmp/liner-door-jail-12345678/root'
        options = fixture.mount_entry('swap '+root+' tmpfs rw,nosuid,size=64m 0\n',root)
        fixture.require_nosuid(options)
        fixture.require_nosuid({'rw','nosetuid','nodevices'})
        for bad in (None,set(),{'nosetuid'},{'nosuid','setuid'},{'nosuid','noexec'},{'nosuid','devices'}):
            with self.assertRaises(ValueError): fixture.require_nosuid(bad)
        for text in ('disk '+root+' zfs rw 0', 'swap '+root+'/proc tmpfs rw 0',
                     ('swap '+root+' tmpfs nosuid 0\n')*2):
            with self.assertRaises(ValueError): fixture.mount_entry(text,root)
        self.assertIsNone(fixture.mount_entry('swap /tmp tmpfs rw 0',root))

    def test_library_inventory_strict_allowlist(self):
        interpreter = fixture.elf_interpreter(elf_image())
        paths = fixture.library_paths(' libc.so.1 => /lib/64/libc.so.1\n',interpreter)
        self.assertEqual(len(paths),2)
        self.assertIn(PurePosixPath('/usr/lib/amd64/ld.so.1'),paths)
        self.assertNotIn(PurePosixPath('/lib/amd64/ld.so.1'),paths)
        for text in ('','libc.so.1 => (file not found)', 'libc => /etc/libc.so',
                     'libc => /lib/64/../../etc/passwd','libc => /lib/64/libc.so trailing'):
            with self.assertRaises(ValueError): fixture.library_paths(text,interpreter)
        with self.assertRaises(ValueError): fixture.library_paths('libc => /lib/64/libc.so.1','/tmp/loader')

    @unittest.skipUnless(os.name == 'posix','POSIX directory permission semantics')
    def test_library_directories_are_searchable_under_private_umask(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)/'root'
            root.mkdir(mode=0o700)
            directories = []
            libraries = fixture.library_paths('libc.so.1 => /lib/64/libc.so.1\n',
                                              PurePosixPath('/usr/lib/amd64/ld.so.1'))
            prior_mask = os.umask(0o077)
            try:
                for library in libraries:
                    destination = fixture.library_destination(root,library,directories)
                    self.assertEqual(destination.relative_to(root).as_posix(),str(library)[1:])
                    destination.write_bytes(b'synthetic')
                fixture.library_destination(root,libraries[0],directories)
                private = Path(temporary)/'private'
                private.mkdir(mode=0o755)
            finally:
                os.umask(prior_mask)
            self.assertEqual(stat.S_IMODE(private.stat().st_mode),0o700)
            self.assertEqual(stat.S_IMODE(root.stat().st_mode),0o700)
            self.assertEqual([p.relative_to(root).as_posix() for p in directories],
                             ['lib','lib/64','usr','usr/lib','usr/lib/amd64'])
            for directory in directories:
                self.assertEqual(stat.S_IMODE(directory.stat().st_mode),0o755,directory)

    def test_exact_elf_interpreter_aliases_are_preserved(self):
        for prefix in ('/usr/lib/amd64','/usr/lib/64','/lib/amd64','/lib/64'):
            path = prefix+'/ld.so.1'
            self.assertEqual(str(fixture.elf_interpreter(elf_image(path))),path)

    def test_malformed_or_unknown_elf_is_refused_before_mount(self):
        values = [b'',elf_image()[:63],elf_image()[:-1],elf_image('/tmp/loader'),
                  elf_image('/usr/lib/amd64/../amd64/ld.so.1'),elf_image(count=0),elf_image(count=2),
                  elf_image('/usr/lib/amd64/ld.so.1\0extra')]
        for offset, fmt, value in ((4,'B',1),(5,'B',2),(16,'H',1),(18,'H',2),(20,'I',2),
                                   (32,'Q',2**63),(52,'H',63),(54,'H',55),(56,'H',129),
                                   (64,'I',1),(72,'Q',2**63),(96,'Q',1)):
            damaged = bytearray(elf_image()); struct.pack_into('<'+fmt,damaged,offset,value)
            values.append(bytes(damaged))
        for value in values:
            with self.subTest(value=value[:64]):
                with self.assertRaises(ValueError): fixture.elf_interpreter(value)

    def test_success_is_only_credential_namespace_result(self):
        result = fixture.evaluate(matrix())
        self.assertTrue(result['credential_namespace_confined'])
        for key in ('broker_isolation_verified','door_backend_enabled','lifecycle_verified'):
            self.assertFalse(result[key])

    def test_every_identity_and_namespace_escape_detected(self):
        for key in fixture.ID_FIELDS | {'groups'} | fixture.FLAGS:
            rows = matrix()
            rows['uid-bbs']['jailed'][key] = False if key in fixture.FLAGS else 61002
            self.assertEqual(fixture.evaluate(rows)['failed_cases'],['uid-bbs'])

    def test_denied_exec_needs_successful_plain_and_setid_controls(self):
        rows = matrix(); rows['uid-root']['jailed'] = {'exec_denied':True}
        self.assertTrue(fixture.evaluate(rows)['credential_namespace_confined'])
        rows['plain']['jailed'] = {'exec_denied':True}
        with self.assertRaises(ValueError): fixture.evaluate(rows)
        rows = matrix(); rows['gid-bbs']['baseline']['egid'] = 61001
        with self.assertRaises(ValueError): fixture.evaluate(rows)

    def test_incomplete_or_mistyped_evidence_not_success(self):
        rows = matrix(); del rows['gid-root']
        with self.assertRaises(ValueError): fixture.evaluate(rows)
        for value in ({}, {'exec_denied':1}, {'exec_denied':True,'errno':2},
                      dict(matrix()['plain']['jailed'],namespace_closed=1)):
            rows = matrix(); rows['plain']['jailed'] = value
            with self.assertRaises(ValueError): fixture.evaluate(rows)

    def test_command_failure_and_timeout_leave_bounded_evidence(self):
        process = Mock(pid=234,returncode=9)
        process.communicate.return_value = ('synthetic output','loader problem')
        with patch.object(fixture.subprocess,'Popen',return_value=process) as start:
            report = {}
            with self.assertRaises(subprocess.CalledProcessError): fixture.command(['/fixed/probe'],report)
            self.assertEqual(report['commands'][0]['stderr'],'loader problem')
            self.assertTrue(start.call_args.kwargs['close_fds'])
        process.communicate.side_effect = [subprocess.TimeoutExpired('probe',1),('','x'*70000)]
        with patch.object(fixture.subprocess,'Popen',return_value=process), \
             patch.object(fixture.os,'killpg',create=True) as kill, \
             patch.object(fixture.signal,'SIGKILL',9,create=True):
            report = {}
            with self.assertRaises(subprocess.TimeoutExpired): fixture.command(['/fixed/probe'],report)
            self.assertEqual(len(report['commands'][0]['stderr']),65536)
            self.assertTrue(report['commands'][0]['timeout']); kill.assert_called_once_with(234,9)

    def test_real_child_diagnostics_preserved_without_becoming_success(self):
        # Exercise the actual process/receipt path on Hosted CI as well as local
        # runs. These are synthetic messages, not a Solaris confinement claim.
        diagnostics = ('JAIL_PROGRESS stage=exec\n'
                       'JAIL_PARENT stage=ready poll=0 poll_errno=0 revents=0 received=0 read_errno=0 marker=0\n'
                       'JAIL_PARENT stage=cleanup-kill result=0 errno=0\n')
        for code in (0,78):
            report = {}
            script = ('import sys; sys.stdout.write(\'{"synthetic":true}\\n\'); '
                      'sys.stderr.write('+repr(diagnostics)+'); sys.exit('+str(code)+')')
            if code:
                with self.assertRaises(subprocess.CalledProcessError):
                    fixture.command([sys.executable,'-c',script],report)
            else:
                result = fixture.command([sys.executable,'-c',script],report)
                self.assertEqual(json.loads(result.stdout),{'synthetic':True})
            saved = json.loads(json.dumps(report))['commands'][0]
            self.assertEqual(saved['exit_code'],code)
            self.assertEqual(saved['stderr'],diagnostics)
            self.assertEqual(json.loads(saved['stdout']),{'synthetic':True})

    def failed_run(self, mounted=False, unmount_failure=False):
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary)/'private'; work.mkdir()
            base = Mock()
            base.compiler_path.return_value = Path('/compiler')
            def remove(paths):
                for path in paths:
                    path.chmod(0o600); path.unlink()
            base.remove_fixture_files.side_effect = remove
            calls = []
            def fake_command(args, report, timeout=15):
                calls.append([str(a).replace(chr(92), '/') for a in args])
                if Path(args[0]) == Path('/compiler'):
                    Path(args[-1]).write_bytes(elf_image())
                    if not mounted: raise subprocess.CalledProcessError(1,args)
                if str(args[0]).endswith('umount') and unmount_failure:
                    raise subprocess.CalledProcessError(1,args)
                output = 'libc.so.1 => /lib/64/libc.so.1\n' if str(args[0]).endswith('ldd') else ''
                return subprocess.CompletedProcess(args,0,output,'')
            sequence = [None,{'setuid'},{'setuid'}]
            if not unmount_failure: sequence.append(None)
            with patch.object(fixture.tempfile,'mkdtemp',return_value=str(work)), \
                 patch.object(fixture.Path,'resolve',lambda self,**kwargs:self), \
                 patch.object(fixture,'read_mount',side_effect=sequence), \
                 patch.object(fixture,'command',side_effect=fake_command):
                with self.assertRaises((ValueError,subprocess.CalledProcessError)):
                    fixture.run_locked(base,{'source_sha':'a'*40,'sha256':{}})
            result = json.loads((work/'result.json').read_text())
            self.assertFalse(result['broker_isolation_verified'])
            if unmount_failure:
                self.assertFalse(result['cleanup_complete'])
                self.assertEqual(result['cleanup_error'],'CalledProcessError')
                self.assertTrue((work/'root').exists())
                self.assertTrue((work/'observer').exists())
            else:
                self.assertTrue(result['cleanup_complete'])
                self.assertEqual({p.name for p in work.iterdir()},{'plan.json','result.json'})
            if not mounted:
                self.assertFalse(any(a[0].endswith('mount') for a in calls))
            else:
                self.assertEqual(sum(a[0].endswith('/umount') for a in calls),1)
                self.assertFalse(any('--observe' in a for a in calls))

    def test_compile_failure_has_receipt_without_mount(self):
        self.failed_run()

    def test_rejected_mount_options_unmount_before_successful_cleanup(self):
        self.failed_run(mounted=True)

    def test_unmount_failure_preserves_paths_and_records_incomplete_cleanup(self):
        self.failed_run(mounted=True,unmount_failure=True)

    @unittest.skipUnless(sys.platform.startswith(('linux','freebsd','sunos')),
                         'native UNIX descriptor passing')
    def test_native_pty_descriptor_handoff(self):
        solaris = sys.platform.startswith('sunos')
        compiler = shlex.split(os.environ.get('CC','/usr/bin/gcc' if solaris else 'cc'))
        libraries = ['-lsocket','-lnsl'] if solaris else ['-lutil']
        with tempfile.TemporaryDirectory(prefix='ln-door-handoff-') as temporary:
            binary = Path(temporary)/'probe'
            subprocess.run([*compiler,'-std=c11','-Wall','-Wextra','-Werror',
                            str(ROOT/'tests/door_handoff_probe.c'),'-o',str(binary),*libraries],
                           check=True,timeout=45)
            result = subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('PASS C PTY handoff',result.stdout)
            self.assertFalse(result.stderr,result.stderr)
            print(result.stdout.strip(),flush=True)

    @unittest.skipUnless(sys.platform.startswith('sunos'),'Solaris native compilation only, no root/mount')
    def test_native_compiler_and_unprivileged_refusal(self):
        self.assertNotEqual(os.getuid(),0)
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary)/'probe'
            subprocess.run(['/usr/bin/gcc','-m64','-std=c11','-Wall','-Wextra','-Werror',
                            str(ROOT/'tests/solaris_door_jail_probe.c'),'-o',str(binary)],check=True,timeout=45)
            result = subprocess.run([str(binary),'--observe','/','plain'],timeout=5)
            self.assertEqual(result.returncode,80)
            result = subprocess.run([str(binary),'--launch-lifecycle','/'],timeout=5)
            self.assertEqual(result.returncode,80)
            result = subprocess.run([str(binary),'--inside-lifecycle'],capture_output=True,timeout=5)
            self.assertEqual((result.returncode,result.stdout),(82,b''))
            interpreter = fixture.elf_interpreter(binary.read_bytes())
            libraries = subprocess.run(['/usr/bin/ldd',str(binary)],capture_output=True,
                                       text=True,check=True,timeout=10).stdout
            self.assertIn(interpreter,fixture.library_paths(libraries,interpreter))
            print('SOLARIS_JAIL_INTERPRETER '+str(interpreter),flush=True)

            # The reporter protocol and diagnostic channel are independent even
            # without root. Namespace flags must NOT claim isolation on the host.
            # / is a Solaris no-op and is not a privilege-denial test. Require
            # an existing distinct /lib target; the expected denial stays true.
            current, target = os.stat('/'), os.stat('/lib')
            self.assertNotEqual((current.st_dev,current.st_ino),(target.st_dev,target.st_ino))
            with subprocess.Popen([str(binary),'--inside'],stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,stderr=subprocess.PIPE) as child:
                self.assertEqual(child.stdout.read(1),b'R')
                value = struct.unpack('=iiii',child.stdout.read(16))
                self.assertEqual(value[1:],(0,1,1))
                output, errors = child.communicate(b'A',timeout=5)
                self.assertEqual((child.returncode,output),(0,b''))
                expected = ['payload-entry','priv-allocate','priv-basicset',
                            'priv-query','priv-done','payload-namespace',
                            'payload-descriptors','payload-chroot-target',
                            'payload-chroot','payload-ready','payload-ack-wait',
                            'payload-ack-done']
                self.assertEqual(errors.decode().splitlines(),
                                 ['JAIL_PROGRESS stage='+stage for stage in expected])

            # Diagnostics cannot turn a bad acknowledgement into success or
            # contaminate the binary observation protocol on stdout.
            result = subprocess.run([str(binary),'--inside'],input=b'X',
                                    capture_output=True,timeout=5)
            self.assertEqual(result.returncode,72)
            self.assertEqual(result.stdout[:1],b'R')
            self.assertEqual(len(result.stdout),17)
            self.assertIn(b'JAIL_PROGRESS stage=payload-ack-done\n',result.stderr)

            # A saturated diagnostic pipe must not stall the payload. The root
            # observer also uses O_NONBLOCK on this pipe; no root/mount needed.
            read_fd, write_fd = os.pipe()
            try:
                os.set_blocking(write_fd,False)
                while True:
                    try: os.write(write_fd,b'x'*4096)
                    except BlockingIOError: break
                result = subprocess.run([str(binary),'--inside'],input=b'A',
                                        stdout=subprocess.PIPE,stderr=write_fd,timeout=5)
                self.assertEqual(result.returncode,0)
                self.assertEqual(result.stdout[:1],b'R')
                self.assertEqual(struct.unpack('=iiii',result.stdout[1:])[1:],(0,1,1))
            finally:
                os.close(write_fd); os.close(read_fd)


    @unittest.skipUnless(sys.platform.startswith('sunos'),'Solaris UNIX peer credentials')
    def test_native_door_client_rejects_nonroot_launcher(self):
        import socket
        self.assertNotEqual(os.getuid(),0)
        with tempfile.TemporaryDirectory(prefix='ln-door-client-') as temporary:
            binary = Path(temporary)/'probe'
            endpoint = str(Path(temporary)/'service')
            subprocess.run(['/usr/bin/gcc','-m64','-std=c11','-Wall','-Wextra','-Werror',
                            '-I/usr/openssl/3/include',str(ROOT/'tests/door_service_client_probe.c'),
                            '-L/usr/openssl/3/lib/amd64','-Wl,-R,/usr/openssl/3/lib/amd64',
                            '-lcrypto','-lsocket','-lnsl','-o',str(binary)],check=True,timeout=45)
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as listener:
                listener.bind(endpoint)
                listener.listen(1)
                listener.settimeout(3)
                with subprocess.Popen([str(binary),endpoint,'deny'],stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE) as probe:
                    try:
                        connection,_ = listener.accept()
                        with connection:
                            connection.settimeout(3)
                            self.assertEqual(connection.recv(1),b'','Request reached a non-root peer')
                        output, errors = probe.communicate(timeout=3)
                        self.assertEqual((probe.returncode,output,errors),(0,b'DENIED non-root launcher\n',b''))
                    finally:
                        if probe.poll() is None:
                            probe.kill()
                            probe.wait(timeout=3)


if __name__ == '__main__': unittest.main()
