"""Cross-OS release-candidate set regression tests using synthetic artifacts."""
import hashlib
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from release_manifest import create
from verify_release_set import verify_set

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
