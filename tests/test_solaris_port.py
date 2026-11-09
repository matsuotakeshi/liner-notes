#!/usr/bin/env python3
"""Hosted-only Solaris build/CI contracts. Does not contact a VM."""
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch
import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ci"))
import native_ci
import native_remote
import solaris_ci


class SolarisContracts(unittest.TestCase):
    @unittest.skipUnless(shutil.which("cc") or shutil.which("gcc"), "host C preprocessor required")
    def test_door_lock_feature_namespaces(self):
        # Exercise the actual source preamble, not a duplicated macro model.
        # FreeBSD's fcntl.h requires the default BSD-visible namespace for flock.
        preamble = (ROOT / "src/door_lock.c").read_text().split('#include "door_lock.h"', 1)[0]
        compiler = shutil.which("cc") or shutil.which("gcc")
        for platform, flags in (("FreeBSD", ["-D__FreeBSD__=1"]),
                                ("Linux", []), ("Solaris", ["-D__sun=1"])):
            with self.subTest(platform=platform):
                macros = subprocess.check_output([compiler, "-E", "-dM", "-x", "c", "-U__FreeBSD__", "-U__sun", *flags, "-"],
                                                 input=preamble, text=True)
                if platform == "FreeBSD":
                    self.assertNotIn("#define _XOPEN_SOURCE", macros)
                    self.assertNotIn("#define _POSIX_C_SOURCE", macros)
                else:
                    self.assertIn("#define _XOPEN_SOURCE 700", macros)
                if platform == "Solaris":
                    self.assertIn("#define __EXTENSIONS__ 1", macros)

    def test_separate_target_preserves_three_os_default(self):
        self.assertEqual(native_ci.DEFAULT_TARGETS, ("freebsd", "rocky", "ubuntu"))
        self.assertEqual(native_ci.TARGETS["solaris"],
                         ("ci-test@10.0.0.52", "/home/ci/.ssh/id_ed25519_solaris_ci"))
        self.assertEqual(native_remote.commands_for("solaris"),
                         [[sys.executable, "ci/solaris_ci.py", "inventory"],
                          [sys.executable, "ci/solaris_ci.py", "test"]])

    def test_solaris_version_and_architecture_gate(self):
        with patch.object(native_remote.platform, "system", return_value="SunOS"), \
             patch.object(native_remote.platform, "release", return_value="5.11"), \
             patch.object(native_remote.platform, "machine", return_value="i86pc"), \
             patch.object(Path, "read_text", return_value="Oracle Solaris 11.4 CBE"):
            native_remote.verify_os("solaris")
            with patch.object(native_remote.platform, "machine", return_value="sun4v"):
                with self.assertRaises(ValueError):
                    native_remote.verify_os("solaris")
            with patch.object(Path, "read_text", return_value="Oracle Solaris 11.3"):
                with self.assertRaises(ValueError):
                    native_remote.verify_os("solaris")

    def test_main_guard_before_any_private_runner(self):
        for filename in ("solaris-ssh-test.yml", "solaris-build-test.yml"):
            document = yaml.safe_load((ROOT / ".github/workflows" / filename).read_text())
            for job in document["jobs"].values():
                if "self-hosted" in job.get("runs-on", []):
                    self.assertIn("github.ref == 'refs/heads/main'", job["if"])
                    self.assertIn("github.repository == 'matsuotakeshi/liner-notes'", job["if"])
            triggers = document.get("on", document.get(True))
            self.assertNotIn("pull_request", triggers)
            self.assertNotIn("pull_request_target", triggers)

    def test_build_uses_native_libc_pty_and_socket_libraries(self):
        with patch.object(solaris_ci, "find_tool", side_effect=["/usr/bin/gmake", "/usr/bin/gcc"]), \
             patch.object(solaris_ci.shutil, "which", return_value=None), \
             patch.object(Path, "glob", return_value=[]), \
             patch.object(Path, "is_file", return_value=False):
            args = solaris_ci.build_arguments()
        self.assertIn("PTY_LIBS=", args)
        self.assertIn("LDLIBS=-lsocket -lnsl", args)
        self.assertIn("CFLAGS=-O2 -g -m64", args)
        self.assertNotIn("-Wno-error", " ".join(args))

    def test_missing_tool_is_explicit_and_never_installs(self):
        with patch.object(solaris_ci.shutil, "which", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "Required installed tool missing"):
                solaris_ci.find_tool(["gmake"])


if __name__ == "__main__":
    unittest.main()
