"""Real compiled ELF fixture tests, without FreeBSD hosts, pkg or root access."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'ci'))
from freebsd_fixture_libraries import seed_libraries, verify_libraries


class Names(unittest.TestCase):
    def test_missing_or_unsafe_manifest_is_rejected_before_filesystem_access(self):
        for required in (None, [], ['../libc.so.7'], ['libc.so.7;cmd'], ['libc.so.7:32'], 'libc.so.7'):
            with self.assertRaises(ValueError):
                seed_libraries(Path('/nonexistent-fixture'), required)


@unittest.skipUnless(sys.platform.startswith('linux'),
                     'Real amd64 ELF compilation runs on hosted Linux')
class Libraries(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc') or not shutil.which('readelf'):
            raise RuntimeError('Hosted ELF regression requires cc and readelf')

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='fixture-libs-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.host = self.base/'host'
        self.directories = [self.host/'lib', self.host/'usr/lib', self.host/'usr/local/lib']
        for path in self.directories:
            path.mkdir(parents=True)
        self.root = self.base/'isolated'
        self.root.mkdir()
        self.library('libdependency.so.1', 'int dependency(void) { return 7; }', self.directories[1])
        self.library('libfixture.so.1', 'extern int dependency(void); int fixture(void) { return dependency(); }',
                     self.directories[0], ['-L'+str(self.directories[1]), '-l:libdependency.so.1'])

    def library(self, name, source, directory, flags=()):
        code = self.base/(name+'.c')
        code.write_text(source)
        subprocess.run(['cc', '-shared', '-fPIC', '-nostdlib', '-Wl,-soname,'+name,
                        '-o', str(directory/name), str(code), *flags], check=True, capture_output=True)

    def seed(self, required=None):
        return seed_libraries(self.root, required or ['libfixture.so.1'], self.directories,
                              shutil.which('readelf'))

    def state(self):
        return {str(p.relative_to(self.host)):(hashlib.sha256(p.read_bytes()).hexdigest(),p.stat().st_mode,p.stat().st_mtime_ns)
                for p in self.host.rglob('*') if p.is_file()}

    def test_actual_elf_dependency_closure_is_copied_without_modifying_source(self):
        before = self.state()
        records = self.seed()
        self.assertEqual(set(records), {'libfixture.so.1', 'libdependency.so.1'})
        self.assertEqual(before, self.state())
        verify_libraries(self.root, records)
        for record in records.values():
            copied = self.root/record['path']
            self.assertEqual(copied.read_bytes(), Path(record['source']).read_bytes())
            self.assertNotEqual(copied.stat().st_ino, Path(record['source']).stat().st_ino)
            self.assertEqual(copied.stat().st_mode & 0o777, 0o444)

    def test_port_symlink_materializes_real_bytes_in_ports_layout(self):
        self.library('libport.so.1', 'int port(void) { return 1; }', self.directories[2])
        actual = self.directories[2]/'libport.so.1.2'
        (self.directories[2]/'libport.so.1').rename(actual)
        (self.directories[2]/'libport.so.1').symlink_to(actual.name)
        records = self.seed(['libport.so.1'])
        self.assertEqual(records['libport.so.1']['path'], 'usr/local/lib/libport.so.1')
        self.assertFalse((self.root/'usr/local/lib/libport.so.1').is_symlink())

    def test_missing_transitive_library_and_mismatched_soname_are_rejected(self):
        (self.directories[1]/'libdependency.so.1').unlink()
        with self.assertRaisesRegex(ValueError, 'no real host provider'):
            self.seed()
        shutil.copyfile(self.directories[0]/'libfixture.so.1', self.directories[0]/'libwrong.so.1')
        with self.assertRaisesRegex(ValueError, 'SONAME differs'):
            self.seed(['libwrong.so.1'])

    def test_fake_or_escaping_provider_is_rejected(self):
        fake = self.directories[0]/'libfake.so.1'
        fake.write_text('not a shared library')
        with self.assertRaisesRegex(ValueError, 'ELF'):
            self.seed(['libfake.so.1'])
        fake.unlink()
        outside = self.base/'outside'
        shutil.copyfile(self.directories[0]/'libfixture.so.1', outside)
        fake.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'escapes'):
            self.seed(['libfake.so.1'])

    def test_existing_destination_and_directory_escape_are_rejected(self):
        (self.root/'lib').mkdir()
        kept = self.root/'lib/libfixture.so.1'
        kept.write_bytes(b'keep')
        with self.assertRaises(FileExistsError):
            self.seed()
        self.assertEqual(kept.read_bytes(), b'keep')
        kept.unlink()
        (self.root/'lib').rmdir()
        (self.root/'lib').symlink_to(self.directories[0], target_is_directory=True)
        before = self.state()
        with self.assertRaisesRegex(ValueError, 'escapes'):
            self.seed()
        self.assertEqual(before, self.state())

    def test_later_tampering_is_detected(self):
        records = self.seed()
        copied = self.root/'lib/libfixture.so.1'
        copied.chmod(0o600)
        copied.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'bytes changed'):
            verify_libraries(self.root, records)


if __name__ == '__main__':
    unittest.main()
