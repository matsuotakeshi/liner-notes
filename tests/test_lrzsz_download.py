"""Pinned download/cache failures must never weaken archive or TLS verification."""
import hashlib
import http.client
import importlib.util
from pathlib import Path
import ssl
import tempfile
import unittest
import urllib.error
import urllib.request
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('lrzsz_download', ROOT/'scripts/build-lrzsz.py')
build = importlib.util.module_from_spec(spec); spec.loader.exec_module(build)


class Retrieval(unittest.TestCase):
    def setUp(self):
        self.data = b'fixed test archive'
        self.digest = patch.object(build, 'SHA256', hashlib.sha256(self.data).hexdigest())
        self.digest.start(); self.addCleanup(self.digest.stop)
        self.opener = Mock()
        self.open_patch = patch.object(build.urllib.request, 'build_opener', return_value=self.opener)
        self.open_patch.start(); self.addCleanup(self.open_patch.stop)
        self.sleep_patch = patch.object(build.time, 'sleep')
        self.sleep = self.sleep_patch.start(); self.addCleanup(self.sleep_patch.stop)

    def response(self, data=None):
        response = Mock()
        response.__enter__ = Mock(return_value=response)
        response.__exit__ = Mock(return_value=False)
        response.read.return_value = self.data if data is None else data
        return response

    def test_success_uses_pinned_url_timeout_and_read_limit(self):
        response = self.response(); self.opener.open.return_value = response
        self.assertEqual(build.download_archive(), self.data)
        self.opener.open.assert_called_once_with(build.URL, timeout=30)
        response.read.assert_called_once_with(build.MAX_ARCHIVE + 1)
        self.sleep.assert_not_called()

    def test_timeout_and_connection_failure_retry_then_verify(self):
        self.opener.open.side_effect = [TimeoutError(), urllib.error.URLError(ConnectionResetError()), self.response()]
        self.assertEqual(build.download_archive(), self.data)
        self.assertEqual(self.opener.open.call_count, 3)
        self.assertEqual([c.args[0] for c in self.sleep.call_args_list], [2, 5])

    def test_retry_exhaustion_stays_failure(self):
        self.opener.open.side_effect = TimeoutError()
        with self.assertRaises(TimeoutError): build.download_archive()
        self.assertEqual(self.opener.open.call_count, 3)
        self.assertEqual(self.sleep.call_count, 2)

    def test_transient_http_retry(self):
        self.opener.open.side_effect = [urllib.error.HTTPError(build.URL, 503, 'Unavailable', {}, None), self.response()]
        self.assertEqual(build.download_archive(), self.data)
        self.assertEqual(self.opener.open.call_count, 2)

    def test_http_404_is_not_retried(self):
        self.opener.open.side_effect = urllib.error.HTTPError(build.URL, 404, 'Missing', {}, None)
        with self.assertRaises(urllib.error.HTTPError): build.download_archive()
        self.sleep.assert_not_called(); self.assertEqual(self.opener.open.call_count, 1)

    def test_bad_certificate_is_not_retried(self):
        self.opener.open.side_effect = urllib.error.URLError(ssl.SSLCertVerificationError('bad certificate'))
        with self.assertRaises(urllib.error.URLError): build.download_archive()
        self.sleep.assert_not_called(); self.assertEqual(self.opener.open.call_count, 1)

    def test_incomplete_network_read_is_retried(self):
        response = self.response(); response.read.side_effect = http.client.IncompleteRead(b'part', 100)
        self.opener.open.side_effect = [response, self.response()]
        self.assertEqual(build.download_archive(), self.data)
        self.assertEqual(self.opener.open.call_count, 2)

    def test_wrong_hash_and_oversize_are_not_retried(self):
        for data in (b'tampered', b'x'*(build.MAX_ARCHIVE+1)):
            self.opener.reset_mock(); self.sleep.reset_mock()
            self.opener.open.return_value = self.response(data)
            with self.assertRaises(ValueError): build.download_archive()
            self.sleep.assert_not_called(); self.assertEqual(self.opener.open.call_count, 1)

    def test_plain_http_redirect_is_refused_before_fetch(self):
        handler = build.HTTPSRedirect()
        with self.assertRaisesRegex(ValueError, 'remain HTTPS'):
            handler.redirect_request(urllib.request.Request(build.URL), None, 302, '', {}, 'http://example.org/archive')

    def test_verified_cache_hit_has_no_network(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory)/'source.tar.gz'; archive.write_bytes(self.data)
            build.ensure_archive(archive)
            self.opener.open.assert_not_called()
            self.assertEqual(archive.read_bytes(), self.data)

    def test_bad_cache_is_preserved_and_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory)/'source.tar.gz'; archive.write_bytes(b'corrupt')
            with self.assertRaisesRegex(ValueError, 'SHA-256'): build.ensure_archive(archive)
            self.assertEqual(archive.read_bytes(), b'corrupt')
            self.opener.open.assert_not_called()

    def test_download_publishes_verified_file_and_reuses_it(self):
        self.opener.open.return_value = self.response()
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory)/'cache'/'source.tar.gz'
            build.ensure_archive(archive); build.ensure_archive(archive)
            self.assertEqual(archive.read_bytes(), self.data)
            self.assertEqual(list(archive.parent.iterdir()), [archive])
            self.assertEqual(self.opener.open.call_count, 1)

    def test_bad_download_never_publishes_cache(self):
        self.opener.open.return_value = self.response(b'corrupt')
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory)/'source.tar.gz'
            with self.assertRaises(ValueError): build.ensure_archive(archive)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_local_archive_also_requires_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory)/'source.tar.gz'; archive.write_bytes(self.data)
            self.assertEqual(build.read_archive(archive), self.data)
            archive.write_bytes(b'corrupt')
            with self.assertRaises(ValueError): build.read_archive(archive)


class WorkflowContracts(unittest.TestCase):
    def test_cache_key_and_both_consumers_use_pinned_source(self):
        for workflow in ('linux.yml', 'selinux.yml'):
            text = (ROOT/'.github/workflows'/workflow).read_text(encoding='utf-8')
            self.assertIn('key: lrzsz-source-0.13.1-' + build.SHA256, text)
            self.assertIn('--download-archive build/lrzsz-source/lrzsz-0.13.1.tar.gz', text)
            self.assertNotIn('restore-keys:', text)
        self.assertIn('--archive /source/build/lrzsz-source/lrzsz-0.13.1.tar.gz',
                      (ROOT/'ci/linux.sh').read_text(encoding='utf-8'))
        self.assertIn('--archive build/lrzsz-source/lrzsz-0.13.1.tar.gz',
                      (ROOT/'.github/workflows/selinux.yml').read_text(encoding='utf-8'))


if __name__ == '__main__': unittest.main()
