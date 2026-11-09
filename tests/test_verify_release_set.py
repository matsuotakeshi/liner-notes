"""Cross-OS release-candidate set regression tests using synthetic artifacts."""
import hashlib
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import json
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from release_manifest import create
from verify_release_set import verify_set
import release_manifest

SHA = "a" * 40
SPECS = {
    "debian13": {
        "liner-notes": "liner-notes_0.74-1_amd64.deb",
        "liner-notes-web": "liner-notes-web_0.74-1_all.deb",
        "liner-notes-link": "liner-notes-link_0.74-1_all.deb",
    },
    "rocky10": {
        "liner-notes": "liner-notes-0.74-1.el10.x86_64.rpm",
        "liner-notes-web": "liner-notes-web-0.74-1.el10.x86_64.rpm",
        "liner-notes-link": "liner-notes-link-0.74-1.el10.x86_64.rpm",
    },
    "freebsd15": {"liner-notes": "liner-notes-0.74.pkg"},
}


def fixture(root, platform, source_sha=SHA):
    folder = root / platform
    folder.mkdir()
    version = "0.74" if platform == "freebsd15" else "0.74-1"
    sums = []
    for name, filename in SPECS[platform].items():
        data = (platform + "-" + name).encode()
        (folder / filename).write_bytes(data)
        sums.append(hashlib.sha256(data).hexdigest() + "  " +
                    ("./" if platform != "freebsd15" else "") + filename)
    create(SimpleNamespace(platform=platform, source_sha=source_sha,
                           version=version, directory=str(folder),
                           package=[name + "=" + filename for name, filename in SPECS[platform].items()]))
    (folder / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="ascii")
    info = "source_sha=" + source_sha + "\n"
    if platform == "freebsd15":
        # Match the real FreeBSD CI BUILD-INFO.txt key with digits.
        info += "platform=freebsd15\nsource_archive_sha256=" + "f" * 64 + "\n"
    (folder / "BUILD-INFO.txt").write_text(info, encoding="utf-8")
    return folder


class ReleaseSetTests(unittest.TestCase):
    def optional_fixture(self, root, source_sha=SHA):
        folder = root / "rocky10-selinux"
        folder.mkdir()
        filename = "liner-notes-selinux-0.1.0-1.el10.noarch.rpm"
        data = b"synthetic policy; tests mock only the RPM header reader"
        (folder / filename).write_bytes(data)
        create(SimpleNamespace(platform="rocky10-selinux", source_sha=source_sha, version="0.1.0-1.el10",
                               directory=str(folder), package=["liner-notes-selinux=" + filename]))
        (folder / "SHA256SUMS").write_text(hashlib.sha256(data).hexdigest()+"  "+filename+"\n")
        (folder / "BUILD-INFO.txt").write_text("source_sha="+source_sha+"\n")
        return folder

    def test_optional_policy_missing_is_not_verified_and_required_is_rejected(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for platform in SPECS:
                fixture(root, platform)
            self.assertEqual(verify_set(root, SHA)["components"], {})
            with self.assertRaisesRegex(ValueError, "Required SELinux"):
                verify_set(root, SHA, require_selinux=True)
            result = subprocess.run([sys.executable, "-B", str(Path(__file__).resolve().parents[1] /
                                     "scripts/verify_release_set.py"), "--root", str(root),
                                     "--source-sha", SHA, "--require-selinux"], capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn("Required SELinux", result.stderr)

    @patch.object(release_manifest.subprocess, "run", return_value=SimpleNamespace(
        returncode=0, stdout="liner-notes-selinux|0.1.0-1.el10|noarch"))
    def test_optional_policy_keeps_its_own_version_and_exact_sha(self, query):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for platform in SPECS:
                fixture(root, platform)
            folder = self.optional_fixture(root)
            result = verify_set(root, SHA, require_selinux=True)
            self.assertEqual(result["version"], "0.74")
            self.assertEqual(set(result["platforms"]), set(SPECS))
            component = result["components"]["rocky10-selinux"]
            self.assertEqual(component["version"], "0.1.0-1.el10")
            self.assertEqual(component["source_sha"], SHA)
            self.assertEqual(set(component["packages"]), {"liner-notes-selinux"})
            manifest = folder / "RELEASE-MANIFEST.json"
            document = json.loads(manifest.read_text())
            document["source_sha"] = "b" * 40
            manifest.write_text(json.dumps(document))
            with self.assertRaisesRegex(ValueError, "Source revision"):
                verify_set(root, SHA)

    @patch.object(release_manifest.subprocess, "run", return_value=SimpleNamespace(
        returncode=0, stdout="liner-notes-selinux|0.1.0-1.el10|noarch"))
    def test_optional_policy_rejects_hash_build_info_or_version_tampering(self, query):
        for tampering in ("hash", "build", "version", "missing"):
            with self.subTest(tampering=tampering), tempfile.TemporaryDirectory() as path:
                root = Path(path)
                for platform in SPECS:
                    fixture(root, platform)
                folder = self.optional_fixture(root)
                if tampering == "hash":
                    next(folder.glob("*.rpm")).write_bytes(b"changed")
                elif tampering == "build":
                    (folder / "BUILD-INFO.txt").write_text("source_sha="+"b"*40+"\n")
                elif tampering == "version":
                    manifest = folder / "RELEASE-MANIFEST.json"
                    doc = json.loads(manifest.read_text());doc["version"]="0.1.1-1.el10"
                    manifest.write_text(json.dumps(doc))
                else:
                    next(folder.glob("*.rpm")).unlink()
                with self.assertRaises(ValueError):
                    verify_set(root, SHA)

    def test_valid_three_os_release_set(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for platform in SPECS:
                fixture(root, platform)
            result = verify_set(root, SHA)
            self.assertEqual(result["version"], "0.74")
            self.assertEqual(set(result["platforms"]), set(SPECS))

    def test_mismatched_source_sha(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for platform in SPECS:
                fixture(root, platform, "b" * 40 if platform == "rocky10" else SHA)
            with self.assertRaises(ValueError):
                verify_set(root, SHA)

    def test_tampering_and_missing_platform(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for platform in SPECS:
                fixture(root, platform)
            (root / "freebsd15" / "liner-notes-0.74.pkg").write_bytes(b"modified")
            with self.assertRaises(ValueError):
                verify_set(root, SHA)
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for platform in ("rocky10", "debian13"):
                fixture(root, platform)
            with self.assertRaises(ValueError):
                verify_set(root, SHA)

    def test_reject_malformed_or_duplicate_build_info_keys(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for platform in SPECS:
                fixture(root, platform)
            info = root / "freebsd15" / "BUILD-INFO.txt"
            valid = info.read_text(encoding="utf-8")
            for bad in (
                "source_sha=" + SHA + "\nsource_sha=" + SHA + "\n",
                "Source_sha=" + SHA + "\n",
                "1source_sha=" + SHA + "\n",
                "source_sha=" + SHA + "\ninvalid-key=value\n",
            ):
                with self.subTest(bad=bad):
                    info.write_text(bad, encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, "Invalid or duplicate BUILD-INFO key"):
                        verify_set(root, SHA)
            info.write_text(valid, encoding="utf-8")
            self.assertEqual(verify_set(root, SHA)["version"], "0.74")

    def test_reject_mismatched_base_versions(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for platform in SPECS:
                fixture(root, platform)
            folder = root / "freebsd15"
            import json
            manifest = folder / "RELEASE-MANIFEST.json"
            data = json.loads(manifest.read_text())
            data["version"] = "0.75"
            manifest.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaises(ValueError):
                verify_set(root, SHA)

    def test_reject_bad_build_commit(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for platform in SPECS:
                fixture(root, platform)
            (root / "rocky10" / "BUILD-INFO.txt").write_text("source_sha=" + "b" * 40 + "\n")
            with self.assertRaises(ValueError):
                verify_set(root, SHA)

    def test_checksum_and_version_mismatch(self):
        with tempfile.TemporaryDirectory() as path:
            root = Path(path)
            for platform in SPECS:
                fixture(root, platform)
            (root / "debian13" / "SHA256SUMS").write_text("not-a-hash\n")
            with self.assertRaises(ValueError):
                verify_set(root, SHA)


if __name__ == "__main__":
    unittest.main(verbosity=2)
