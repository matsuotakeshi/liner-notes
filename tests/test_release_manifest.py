"""Release artifact manifest integrity and failure-mode tests."""
import argparse
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release_manifest as rm

SOURCE = "a" * 40


class ManifestTests(unittest.TestCase):
    def policy_arguments(self, directory):
        filename = "liner-notes-selinux-0.1.0-1.el10.noarch.rpm"
        (directory / filename).write_bytes(b"synthetic policy RPM; header query mocked")
        return argparse.Namespace(platform="rocky10-selinux", version="0.1.0-1.el10",
                                  source_sha=SOURCE, directory=str(directory),
                                  package=["liner-notes-selinux=" + filename])

    def test_policy_has_independent_version_and_real_header_check(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            args = self.policy_arguments(directory)
            result = argparse.Namespace(returncode=0, stdout="liner-notes-selinux|0.1.0-1.el10|noarch")
            with patch.object(rm.subprocess, "run", return_value=result) as query:
                rm.create(args)
                checked = argparse.Namespace(platform=args.platform, version=args.version, source_sha=SOURCE,
                                             manifest=str(directory / "RELEASE-MANIFEST.json"))
                self.assertEqual(rm.verify(checked)["version"], "0.1.0-1.el10")
                self.assertEqual(query.call_count, 2)
                self.assertEqual(query.call_args.args[0][:3], ["rpm", "-qp", "--qf"])
                checked.source_sha = "b" * 40
                with self.assertRaises(ValueError):
                    rm.verify(checked)

    def test_policy_rejects_wrong_rpm_header_and_unavailable_query(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            args = self.policy_arguments(directory)
            for code, header in [(0, "other|0.1.0-1.el10|noarch"),
                                 (0, "liner-notes-selinux|0.1.1-1.el10|noarch"),
                                 (0, "liner-notes-selinux|0.1.0-1.el10|x86_64"), (1, "")]:
                with self.subTest(header=header), patch.object(rm.subprocess, "run",
                        return_value=argparse.Namespace(returncode=code, stdout=header)):
                    with self.assertRaises(ValueError):
                        rm.create(args)
            with patch.object(rm.subprocess, "run", side_effect=FileNotFoundError):
                with self.assertRaisesRegex(ValueError, "unavailable"):
                    rm.create(args)
            self.assertFalse((directory / "RELEASE-MANIFEST.json").exists())

    def test_policy_is_not_an_extra_core_package(self):
        with tempfile.TemporaryDirectory() as folder:
            args = self.policy_arguments(Path(folder))
            args.platform = "rocky10"
            with self.assertRaisesRegex(ValueError, "Incomplete package set"):
                rm.create(args)

    def test_debian_round_trip_and_tampering(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            files = dict(
                **{"liner-notes": "liner-notes_0.74-1_amd64.deb"},
                **{"liner-notes-web": "liner-notes-web_0.74-1_all.deb"},
                **{"liner-notes-link": "liner-notes-link_0.74-1_all.deb"},
            )
            for package, filename in files.items():
                (directory / filename).write_bytes(package.encode())
            rm.create(argparse.Namespace(platform="debian13", version="0.74-1",
                                         source_sha=SOURCE, directory=str(directory),
                                         package=[f"{name}={file}" for name, file in files.items()]))
            test = argparse.Namespace(platform="debian13", source_sha=SOURCE, version="0.74-1",
                                      manifest=str(directory / "RELEASE-MANIFEST.json"))
            rm.verify(test)
            test.source_sha = "b" * 40
            with self.assertRaises(ValueError):
                rm.verify(test)
            test.source_sha = SOURCE
            (directory / files["liner-notes"]).write_bytes(b"changed")
            with self.assertRaises(ValueError):
                rm.verify(test)

    def test_reject_missing_and_path_escape(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            expected = "liner-notes-0.74-1.el10.x86_64.rpm"
            (directory / expected).write_bytes(b"package")
            args = argparse.Namespace(platform="rocky10", version="0.74-1",
                                      source_sha=SOURCE, directory=str(directory),
                                      package=["liner-notes=" + expected])
            with self.assertRaises(ValueError):
                rm.create(args)
            args.platform = "freebsd15"
            args.package = ["liner-notes=../../secrets.pkg"]
            with self.assertRaises(ValueError):
                rm.create(args)
            target = directory / "liner-notes-0.74.pkg"
            target.symlink_to(directory / expected)
            args.package = ["liner-notes=liner-notes-0.74.pkg"]
            with self.assertRaises(ValueError):
                rm.create(args)


    def test_rocky_architecture_round_trip(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            files = {
                "liner-notes": "liner-notes-0.74-1.el10.x86_64.rpm",
                "liner-notes-web": "liner-notes-web-0.74-1.el10.x86_64.rpm",
                "liner-notes-link": "liner-notes-link-0.74-1.el10.x86_64.rpm",
            }
            for package, filename in files.items():
                (directory / filename).write_bytes(package.encode())
            rm.create(argparse.Namespace(platform="rocky10", version="0.74-1",
                                         source_sha=SOURCE, directory=str(directory),
                                         package=[f"{name}={filename}" for name, filename in files.items()]))
            rm.verify(argparse.Namespace(manifest=str(directory / "RELEASE-MANIFEST.json"),
                                         platform="rocky10", version="0.74-1", source_sha=SOURCE))


    def test_freebsd_candidate_round_trip(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            package = "liner-notes-0.74.pkg"
            (directory / package).write_bytes(b"mock-freebsd-pkg")
            rm.create(argparse.Namespace(platform="freebsd15", version="0.74",
                                         source_sha=SOURCE, directory=str(directory),
                                         package=["liner-notes=" + package]))
            checked = argparse.Namespace(
                manifest=str(directory / "RELEASE-MANIFEST.json"),
                platform="freebsd15", version="0.74", source_sha=SOURCE)
            rm.verify(checked)
            (directory / package).write_bytes(b"changed-pkg")
            with self.assertRaises(ValueError):
                rm.verify(checked)


if __name__ == "__main__":
    unittest.main(verbosity=2)
