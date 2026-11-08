"""Release artifact manifest integrity and failure-mode tests."""
import argparse
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import release_manifest as rm

SOURCE = "a" * 40


class ManifestTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
