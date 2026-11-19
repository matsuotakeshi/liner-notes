"""Mirror switching must preserve Ubuntu suites, signatures and runner isolation."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
import ubuntu_apt_mirror as mirror

SOURCE = '''Types: deb
URIs: mirror+file:/etc/apt/apt-mirrors.txt
Suites: noble noble-updates noble-backports
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg

Types: deb
URIs: http://security.ubuntu.com/ubuntu/
Suites: noble-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
'''


class Mirror(unittest.TestCase):
    def test_preserves_every_non_uri_line_and_is_idempotent(self):
        actual = mirror.rewrite_sources(SOURCE)
        unchanged = lambda s: [line for line in s.splitlines() if not line.startswith('URIs:')]
        self.assertEqual(unchanged(actual), unchanged(SOURCE))
        self.assertEqual(actual.count('URIs: ' + mirror.MIRROR), 2)
        self.assertEqual(mirror.rewrite_sources(actual), actual)

    def test_known_runner_sources(self):
        for uri in ('https://mirrors.edge.kernel.org/ubuntu/',
                    'https://ftp.jaist.ac.jp/pub/Linux/ubuntu/',
                    'http://azure.archive.ubuntu.com/ubuntu/',
                    'https://archive.ubuntu.com/ubuntu',
                    'http://us-east-2.ec2.archive.ubuntu.com/ubuntu/',
                    'mirror+file:/etc/apt/apt-security-mirrors.txt'):
            self.assertEqual(mirror.rewrite_sources('URIs: ' + uri), 'URIs: ' + mirror.MIRROR)

    def test_unknown_or_missing_source_rejected(self):
        for text in ('', 'Types: deb', 'URIs: ',
                     'URIs: https://archive.ubuntu.com.evil.invalid/ubuntu',
                     SOURCE + '\nURIs: https://third-party.invalid/ubuntu\n'):
            with self.assertRaises(ValueError):
                mirror.rewrite_sources(text)

    def test_environment_restriction(self):
        env = {'GITHUB_ACTIONS': 'true', 'RUNNER_ENVIRONMENT': 'github-hosted'}
        release = {'ID': 'ubuntu', 'VERSION_ID': '24.04'}
        mirror.check_environment(env, release, 'x86_64')
        for e, r, arch in (({}, release, 'x86_64'),
                           ({**env, 'RUNNER_ENVIRONMENT': 'self-hosted'}, release, 'x86_64'),
                           (env, {**release, 'VERSION_ID': '22.04'}, 'x86_64'),
                           (env, release, 'aarch64')):
            with self.assertRaises(ValueError):
                mirror.check_environment(e, r, arch)

    def test_backup_and_rejected_configuration(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / 'ubuntu.sources'
            source.write_text(SOURCE, encoding='utf-8')
            with patch.object(mirror, 'SOURCES', source), patch.object(mirror, 'check_environment'), \
                    patch.object(mirror.platform, 'freedesktop_os_release', return_value={}), \
                    patch.object(mirror.os, 'geteuid', return_value=0, create=True):
                mirror.main()
                mirror.main()
                self.assertEqual(source.with_name('ubuntu.sources.liner-notes-original').read_text(), SOURCE)
                bad = SOURCE + '\nURIs: https://third-party.invalid/ubuntu\n'
                source.write_text(bad, encoding='utf-8')
                with self.assertRaises(ValueError):
                    mirror.main()
                self.assertEqual(source.read_text(), bad)


if __name__ == '__main__':
    unittest.main()
