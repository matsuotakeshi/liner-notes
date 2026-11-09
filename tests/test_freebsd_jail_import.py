"""Import the actual jail entry point with -I, without executing jail operations.

Hosted CI supplies /ci/source as a link to this checkout. No mock modules or
PYTHONPATH entries may make a missing dependency appear to pass this test.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class IsolatedImport(unittest.TestCase):
    def test_real_entrypoint_imports_in_isolated_python(self):
        self.assertEqual(Path('/ci/source').resolve(), ROOT,
                         'Hosted fixture must map /ci/source to this checkout')
        code = """
import json, pathlib, runpy, sys
assert sys.flags.isolated == 1
assert '/ci/source/ci' not in sys.path
entry = runpy.run_path('/ci/source/ci/freebsd_upgrade_jail.py', run_name='import_check')
helper = sys.modules['freebsd_upgrade_root']
assert pathlib.Path(helper.__file__).resolve() == pathlib.Path('/ci/source/ci/freebsd_upgrade_root.py').resolve()
assert entry['write_door_fixture'] is helper.write_door_fixture
assert entry['PHASE'] == 'admission' and callable(entry['main'])
assert '/ci/source/ci' in sys.path
print(json.dumps({'isolated': True, 'entrypoint_imported': True, 'main_executed': False}))
"""
        with tempfile.TemporaryDirectory() as temporary:
            # Neither a current-directory nor PYTHONPATH module may supply the
            # dependency. -I must import only the explicitly trusted source.
            Path(temporary, 'freebsd_upgrade_root.py').write_text(
                "raise RuntimeError('Untrusted import path used')\n", encoding='utf8')
            environment = dict(os.environ, PYTHONPATH=temporary)
            result = subprocess.run([sys.executable, '-I', '-c', code], cwd=temporary,
                                    env=environment, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(json.loads(result.stdout), {
            'isolated': True, 'entrypoint_imported': True, 'main_executed': False})


if __name__ == '__main__':
    unittest.main()
