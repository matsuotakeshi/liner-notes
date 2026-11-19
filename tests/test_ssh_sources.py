"""Small boundary/packaging checks; real encrypted connections are tested separately."""
from pathlib import Path
import base64
import importlib.util
import unittest
from ssh_screen import Screen, MAIL_PROMPT

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fingerprint', ROOT/'scripts/ssh-key-fingerprint.py')
fingerprint = importlib.util.module_from_spec(spec); spec.loader.exec_module(fingerprint)


class ScriptedChannel:
    def __init__(self, chunks):
        self.chunks = iter(chunks)
        self.sent = []

    def settimeout(self, seconds):
        self.timeout = seconds

    def sendall(self, data):
        self.sent.append(data)

    def recv(self, size):
        try:
            return next(self.chunks)
        except StopIteration:
            raise TimeoutError('no further test data')


class SshScreenTests(unittest.TestCase):
    notice = (b'\r\n1 new mail message(s) arrived. Use MAIL to check your mailbox.\r\n'
              b'MAIN [bob | Board 1 | Article 0] > ')
    mail = (b'MAIL READ 1\r\nMail 1  From:alice To:bob Read:1 Reply-to:0\r\n'
            b'Subject: ssh-mail\r\nprivate SSH mail\r\nEnd mail\r\n')

    def test_mail_response_after_unsolicited_main_prompt(self):
        wire = self.notice + self.mail + MAIL_PROMPT
        for chunks in ([wire], [bytes([x]) for x in wire]):
            with self.subTest(fragmented=len(chunks) > 1):
                channel = ScriptedChannel(chunks)
                result = Screen(channel).cmd('MAIL READ 1', MAIL_PROMPT)
                self.assertIn(self.mail, result)
                self.assertTrue(result.endswith(MAIL_PROMPT))
                self.assertEqual(channel.sent, [b'MAIL READ 1\r'])

    def test_buffered_notice_and_later_output_are_preserved(self):
        following = b'\r\nLater notification\r\n'
        screen = Screen(ScriptedChannel([self.mail + MAIL_PROMPT + following]))
        screen.buf = self.notice
        result = screen.cmd('MAIL READ 1', MAIL_PROMPT)
        self.assertEqual(result, self.notice + self.mail + MAIL_PROMPT)
        self.assertEqual(screen.buf, following)

    def test_main_notice_cannot_complete_missing_mail_response(self):
        screen = Screen(ScriptedChannel([self.notice]))
        with self.assertRaisesRegex(AssertionError, 'missing marker'):
            screen.cmd('MAIL READ 1', MAIL_PROMPT)


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
