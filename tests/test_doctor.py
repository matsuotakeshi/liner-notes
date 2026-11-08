"""Read-only diagnostics: fixtures, corruption, live WAL and secret isolation."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
import datetime
from contextlib import closing
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('doctor', ROOT/'scripts/doctor.py')
doctor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(doctor)


class DoctorTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='doctor-fixture-')
        self.root = Path(self.tmp.name)
        self.write('/etc/liner-notes/liner-notes.env', 'LINER_NOTES_OPTIONS="--database /var/lib/liner-notes/liner.sqlite 2222"\n')
        uid = os.getuid() if hasattr(os,'getuid') else 0
        gid = os.getgid() if hasattr(os,'getgid') else 0
        self.write('/etc/passwd', '\n'.join(f'{name}:x:{uid}:{gid}::/:/bin/false' for name in ('liner_notes','liner_notes_web','liner_notes_link'))+'\n')
        self.write('/usr/sbin/liner-notes', '#!/bin/sh\nexit 99\n', 0o755)
        self.database()

    def tearDown(self):
        self.tmp.cleanup()

    def write(self,name,data,mode=0o600):
        p=self.root/name.lstrip('/');p.parent.mkdir(parents=True,exist_ok=True)
        p.write_bytes(data.encode() if isinstance(data,str) else data);p.chmod(mode)
        return p

    def database(self):
        p=self.root/'var/lib/liner-notes/liner.sqlite';p.parent.mkdir(parents=True,exist_ok=True)
        with closing(sqlite3.connect(p)) as db:
            db.executescript('CREATE TABLE storage_meta(id INTEGER PRIMARY KEY,version INTEGER); INSERT INTO storage_meta VALUES(1,3); CREATE TABLE streams(id INTEGER,name TEXT,length INTEGER,revision INTEGER); CREATE TABLE blocks(stream INTEGER,number INTEGER,value BLOB);')
            db.commit()
        return p

    def instance(self,*args):
        return doctor.Doctor(doctor.parser().parse_args(['--root',str(self.root),'--os','Linux',*args]))

    def state(self):
        return {str(p.relative_to(self.root)):(hashlib.sha256(p.read_bytes()).hexdigest(),p.stat().st_mode,p.stat().st_mtime_ns) for p in self.root.rglob('*') if p.is_file()}

    def test_clean_database_and_no_source_mutation(self):
        before=self.state()
        report=self.instance('--check-database').run()
        self.assertEqual(report['exit_code'],0,report)
        self.assertEqual(before,self.state())
        self.assertNotIn('link_service',sys.modules)

    def test_wal_commit_is_seen_without_touching_source(self):
        p=self.root/'var/lib/liner-notes/liner.sqlite'
        db=sqlite3.connect(p)
        try:
            db.execute('PRAGMA journal_mode=WAL')
            db.execute('UPDATE storage_meta SET version=99');db.commit()
            before=self.state()
            result=self.instance('--check-database').run()
            self.assertEqual(result['exit_code'],2,result)
            self.assertEqual(before,self.state())
        finally: db.close()

    def test_corruption_missing_and_wrong_schema(self):
        p=self.root/'var/lib/liner-notes/liner.sqlite'
        for data in (b'not a SQLite database',b''):
            p.write_bytes(data)
            self.assertEqual(self.instance('--check-database').run()['exit_code'],2)
        p.unlink()
        self.assertEqual(self.instance('--check-database').run()['exit_code'],2)

    def test_journal_and_resource_limits_are_inconclusive(self):
        self.write('/var/lib/liner-notes/liner.sqlite-journal',b'active transaction')
        report=self.instance('--check-database').run()
        self.assertEqual(report['exit_code'],1)
        self.assertIn('busy',[r['code'] for r in report['results']])
        (self.root/'var/lib/liner-notes/liner.sqlite-journal').unlink()
        self.assertEqual(self.instance('--check-database','--max-database-bytes','1').run()['exit_code'],1)

    def test_concurrent_source_change_is_not_corruption(self):
        original=doctor.sqlite3.connect
        def changing(*args,**kwargs):
            path=self.root/'var/lib/liner-notes/liner.sqlite'
            os.utime(path,ns=(path.stat().st_atime_ns,path.stat().st_mtime_ns+1000000000))
            return original(*args,**kwargs)
        with patch.object(doctor.sqlite3,'connect',changing):
            report=self.instance('--check-database').run()
        self.assertEqual(report['exit_code'],1)
        self.assertEqual(report['results'][-1]['code'],'busy')

    def test_no_shell_network_or_configured_executable_execution(self):
        marker='SECRET-do-not-print'
        self.write('/etc/liner-notes/liner-notes.env',f'LINER_NOTES_OPTIONS="$(touch /tmp/{marker})"\n')
        with patch.object(doctor.subprocess,'run',side_effect=AssertionError('execution')), patch('socket.socket.connect',side_effect=AssertionError('network')):
            report=self.instance('--check-all').run()
        self.assertNotIn(marker,json.dumps(report))
        self.assertEqual(report['exit_code'],2)

    def test_optional_features_not_configured(self):
        report=self.instance('--check-links','--check-doors','--check-transfers').run()
        self.assertEqual(report['exit_code'],0,report)
        self.assertEqual([r['code'] for r in report['results']],['disabled']*3)

    def test_invalid_core_options_are_rejected_without_echo(self):
        for text in ('--unknown SECRET 23','--lang SECRET 23','--bind hostname 23','70000','--database --menu 23','--max-mail -1 23'):
            self.write('/etc/liner-notes/liner-notes.env',f'LINER_NOTES_OPTIONS="{text}"\n')
            report=self.instance('--check-doors').run()
            self.assertEqual(report['exit_code'],2,text)
            self.assertNotIn('SECRET',json.dumps(report))

    def test_freebsd_rc_overrides_config(self):
        self.write('/usr/local/etc/liner-notes/liner_notes.conf','liner_notes_port="2222"\nliner_notes_database="/var/lib/liner-notes/liner.sqlite"\n')
        self.write('/etc/rc.conf','liner_notes_port="23"\nliner_notes_program="/opt/notes/runtime"\n')
        obj=self.instance('--os','FreeBSD','--check-database');obj.load()
        self.assertEqual(obj.core['port'],23)
        self.assertEqual(obj.program,'/opt/notes/runtime')

    def test_doors_validate_slots_duplicates_and_drop_without_launch(self):
        self.write('/games/run',b'ELF-fixture',0o755)
        self.write('/etc/liner-notes/doors.conf','1|0|600|/games/run|/games|Example|dorinfo1:2\n')
        before=self.state()
        obj=self.instance('--check-doors','--doors-config','/etc/liner-notes/doors.conf')
        # Windows has no executable mode bits; native CI exercises real UNIX access.
        if os.name=='nt':
            with patch.object(obj,'access',return_value=True):report=obj.run()
        else:report=obj.run()
        self.assertEqual(report['exit_code'],1,obj.results)
        self.assertEqual(before,self.state())
        for text in ('1|0|600|/games/run|/games|Example|dorinfo1:33\n','1|0|600|/games/run|/games|Example\n1|0|600|/games/run|/games|Duplicate\n','1|6|600|/games/run|/games|Example\n'):
            self.write('/etc/liner-notes/doors.conf',text)
            self.assertEqual(self.instance('--check-doors','--doors-config','/etc/liner-notes/doors.conf').run()['exit_code'],2)

    def test_link_syntax_and_secret_not_echoed(self):
        self.write('/etc/liner-notes/link.toml','[node]\nidentity="SECRET-do-not-print"\n[bbs]\npassword_file="PRIVATE"\n')
        report=self.instance('--check-links','--link-config','/etc/liner-notes/link.toml').run()
        self.assertEqual(report['exit_code'],2)
        self.assertNotIn('SECRET-do-not-print',json.dumps(report))
        self.assertNotIn('PRIVATE',json.dumps(report))

    def test_cli_json_language_and_exit_codes(self):
        for lang in ('ja','en'):
            p=subprocess.run([sys.executable,'-B',str(ROOT/'scripts/doctor.py'),'--root',str(self.root),'--os','Linux','--check-database','--json','--lang',lang],capture_output=True,encoding='utf-8',env=dict(os.environ,PYTHONIOENCODING='utf-8'))
            self.assertEqual(p.returncode,0,p.stderr)
            report=json.loads(p.stdout)
            self.assertTrue(report['read_only'])
            self.assertEqual(report['results'][-1]['message'],doctor.TEXT['ok'][lang=='en'])

    def test_staged_root_escape_rejected(self):
        obj=self.instance('--check-database')
        with self.assertRaises(ValueError):obj.path('/../../outside')

    def test_quota_validation_matches_zero_and_duplicate_rules(self):
        for text,code in [('members=999999\nbody_bytes=262144\n',0),('members=0\n',2),('body_bytes=262145\n',2),('members=10\nmembers=20\n',2),('unknown=1\n',2)]:
            self.write('/etc/liner-notes/limits.conf',text)
            obj=self.instance('--check-config');obj.check_limits('/etc/liner-notes/limits.conf')
            self.assertEqual(any(r['status']=='error' for r in obj.results),code==2)

    def test_solaris_json_is_not_evaluated(self):
        self.write('/etc/liner-notes/solaris.json',json.dumps(dict(program='/opt/liner-notes/sbin/liner-notes',working_directory='/var/opt/liner-notes',arguments=['--database','/var/lib/liner-notes/liner.sqlite','2222'])))
        obj=self.instance('--os','SunOS','--check-database')
        self.assertEqual(obj.run()['exit_code'],0,obj.results)
        self.assertEqual(obj.data_dir,'/var/opt/liner-notes')

    def test_systemd_program_is_not_silently_adopted(self):
        obj=self.instance('--check-config')
        obj.command=lambda name,args:(0,'MainPID=0\nActiveState=inactive\nExecStart={ path=/different/program ; }\n') if name=='systemctl' else None
        obj.load()
        self.assertEqual(obj.program,'/usr/sbin/liner-notes')
        obj.check_config()
        self.assertTrue(any(r['code']=='mismatch' for r in obj.results))

    def test_database_symlink_never_opened(self):
        if os.name=='nt':self.skipTest('UNIX symlink privilege test runs in hosted/native CI')
        p=self.root/'var/lib/liner-notes/liner.sqlite'
        source=p.with_name('original.sqlite');p.rename(source);p.symlink_to(source)
        self.assertEqual(self.instance('--check-database').run()['exit_code'],2)

    def test_missing_web_templates_are_not_reported_as_bbs_failure(self):
        self.write('/etc/liner-notes/web/web.toml','[web]\norigin="https://example.test"\nbase_path="/notes/"\ntemplates="/missing/templates"\n[bbs]\nport=2222\nhost="127.0.0.1"\n')
        obj=self.instance('--check-config','--web-config','/etc/liner-notes/web/web.toml')
        obj.check_web()
        self.assertTrue(any(r['code']=='missing' and r['location']=='/missing/templates' for r in obj.results),obj.results)

    @unittest.skipIf(os.name=='nt','POSIX private-file permissions are verified by hosted/native CI')
    def test_resume_receipt_valid_then_receiver_changed(self):
        receiver=self.write('/usr/bin/lrz',b'not executed',0o755)
        self.write('/usr/bin/lsz',b'not executed',0o755)
        self.write('/usr/bin/helper',b'not executed',0o755)
        spool=self.root/'var/lib/liner-notes/spool';spool.mkdir(mode=0o700)
        self.write('/var/lib/liner-notes/lrz-resume.json',json.dumps(dict(patch='crc32-lp64-v1',sha256=hashlib.sha256(receiver.read_bytes()).hexdigest())))
        obj=self.instance('--check-transfers');obj.core['zmodem-helper']='/usr/bin/helper'
        before=self.state();obj.check_transfers()
        self.assertFalse(any(r['code']=='resume' for r in obj.results),obj.results)
        self.assertEqual(before,self.state())
        receiver.write_bytes(b'upgraded binary')
        obj.results=[];obj.check_transfers()
        self.assertTrue(any(r['code']=='resume' for r in obj.results))

    @unittest.skipIf(os.name=='nt','POSIX private-file permissions are verified by hosted/native CI')
    def test_private_file_and_ancestor_access(self):
        p=self.write('/private/secret','SECRET',0o644)
        obj=self.instance('--check-permissions')
        self.assertFalse(obj.item('permissions','/private/secret',private=True))
        p.chmod(0o600)
        self.assertTrue(obj.item('permissions','/private/secret',private=True))
        # A foreign account may not traverse a 0700 parent even if leaf is 0644.
        p.chmod(0o644);p.parent.chmod(0o700)
        obj.account=lambda name:(123456,{123456})
        self.assertFalse(obj.access(p,'other',4))

    @unittest.skipIf(os.name=='nt','POSIX secret protection required; full certificate tests run in hosted CI')
    def test_link_identity_certificates_and_mismatch(self):
        try:
            from cryptography import x509
            from cryptography.hazmat.primitives import serialization
            from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
            from cryptography.x509.oid import NameOID
        except ImportError:self.skipTest('optional cryptography unavailable; required in dedicated hosted doctor CI')
        def generate(directory,expired=False):
            key=Ed25519PrivateKey.generate();name=x509.Name([x509.NameAttribute(NameOID.COMMON_NAME,'doctor fixture')])
            now=datetime.datetime.now(datetime.timezone.utc)
            cert=x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key()).serial_number(1).not_valid_before(now-datetime.timedelta(days=3)).not_valid_after(now+datetime.timedelta(days=-1 if expired else 3)).sign(key,None)
            self.write(directory+'/identity.key',key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
            self.write(directory+'/identity.crt',cert.public_bytes(serialization.Encoding.PEM))
        generate('/identity');generate('/peer')
        board='a'*64
        cfg=dict(node=dict(identity='/identity'),peers=[dict(certificate='/peer/identity.crt',host='example.invalid',port=9044,boards=[board])])
        obj=self.instance('--check-links');before=self.state()
        obj.check_link_certificates(cfg,{board:1})
        self.assertFalse(any(r['status']=='error' for r in obj.results),obj.results)
        self.assertEqual(before,self.state())
        key=self.root/'identity/identity.key';key.write_bytes((self.root/'peer/identity.key').read_bytes())
        obj.results=[];obj.check_link_certificates(cfg,{board:1})
        self.assertTrue(any(r['code']=='certificate' for r in obj.results))
        generate('/identity');generate('/peer',expired=True)
        obj.results=[];obj.check_link_certificates(cfg,{board:1})
        self.assertTrue(any(r['code']=='certificate' for r in obj.results))

    def test_selinux_not_applicable_and_strict_requirement(self):
        obj=self.instance('--check-selinux','--os','FreeBSD');obj.check_selinux()
        self.assertEqual(obj.results[-1]['code'],'disabled')
        obj=self.instance('--check-selinux','--require-selinux');obj.check_selinux()
        self.assertEqual(obj.results[-1]['status'],'error')

    def test_selinux_policy_missing_and_unconfined_distinct(self):
        obj=self.instance('--check-selinux');obj.pid={'core':42}
        self.write('/proc/42/attr/current','system_u:system_r:unconfined_service_t:s0')
        def query(name,args):
            if name=='getenforce':return 0,'Enforcing\n'
            if name=='semodule':return 0,'other_policy 1.0\n'
            if name=='ls':return 0,'system_u:object_r:default_t:s0 /file\n'
            return None
        obj.command=query;obj.check_selinux()
        self.assertTrue(any(r['code']=='unconfined' and r['status']=='error' for r in obj.results),obj.results)
        self.assertTrue(any(r['detail']=='liner_notes policy module missing' for r in obj.results))


if __name__=='__main__':unittest.main()
