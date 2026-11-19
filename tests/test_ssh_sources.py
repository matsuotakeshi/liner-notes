"""Small boundary/packaging checks; real encrypted connections are tested separately."""
from pathlib import Path
import base64
import importlib.util
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fingerprint', ROOT/'scripts/ssh-key-fingerprint.py')
fingerprint = importlib.util.module_from_spec(spec); spec.loader.exec_module(fingerprint)


class SshSources(unittest.TestCase):
    def test_fingerprint_bound_to_numeric_member(self):
        name = b'ssh-ed25519'
        blob = len(name).to_bytes(4, 'big')+name+(32).to_bytes(4, 'big')+bytes(32)
        public = 'ssh-ed25519 '+base64.b64encode(blob).decode()
        self.assertTrue(fingerprint.row(42, public).startswith('42 '))
        for member, text in [(0, public), (1000000, public), (1, 'from="*" '+public),
                             (1, 'ssh-ed25519 !!!!'), (1, 'ssh-rsa '+public.split()[1])]:
            with self.assertRaises((ValueError, UnicodeError)):
                fingerprint.row(member, text)

    def test_authentication_uses_worker_not_ui_or_private_member_data(self):
        runtime = (ROOT/'src/runtime.c').read_text(encoding='utf-8')
        body = runtime.split('static int ssh_authenticate(', 1)[1].split('static unsigned online', 1)[0]
        self.assertIn('session_submit(', body)
        self.assertIn('OPENSSL_cleanse(q,sizeof(*q))', body)
        self.assertNotIn('members_auth', body)
        self.assertNotIn('session_line', body)
        transport = (ROOT/'src/ssh_transport.c').read_text(encoding='utf-8')
        self.assertIn('SSH_PUBLICKEY_STATE_VALID', transport)
        self.assertIn('s->authenticated &&', transport)
        self.assertNotIn('system(', transport)
        self.assertNotIn('execve(', transport)
        self.assertNotIn('members_', transport)

    def test_not_an_os_shell_and_disabled_by_default(self):
        runtime = (ROOT/'src/runtime.c').read_text(encoding='utf-8')
        config = runtime.split('LnSshConfig ssh_config=', 1)[1].split(';', 1)[0]
        self.assertNotIn('.port=', config)
        self.assertIn('SSH_AUTH_METHOD_PASSWORD', (ROOT/'src/ssh_transport.c').read_text())
        rc = (ROOT/'ports/comms/liner-notes/files/liner_notes.in').read_text()
        self.assertIn(': ${liner_notes_ssh_port:=0}', rc)
        self.assertIn('--ssh-host-key "$liner_notes_ssh_host_key"', rc)


if __name__ == '__main__':
    unittest.main()
