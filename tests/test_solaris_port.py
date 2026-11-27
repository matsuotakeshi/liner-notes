#!/usr/bin/env python3
"""Hosted-only Solaris build/CI contracts. Does not contact a VM."""
from pathlib import Path
import importlib.util
from contextlib import redirect_stdout
from io import StringIO
import os
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

spec = importlib.util.spec_from_file_location("build_lrzsz", ROOT / "scripts/build-lrzsz.py")
build_lrzsz = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_lrzsz)


class SolarisContracts(unittest.TestCase):
    def test_command_duration_and_failure_propagation(self):
        for error in (None, subprocess.CalledProcessError(1, 'gmake')):
            output = StringIO()
            with self.subTest(error=error), redirect_stdout(output), \
                 patch.object(solaris_ci.subprocess, 'run', side_effect=error), \
                 patch('time.monotonic', side_effect=[10, 12.5]):
                if error:
                    with self.assertRaises(subprocess.CalledProcessError):
                        solaris_ci.run(['gmake', 'test-doors'])
                else:
                    solaris_ci.run(['gmake', 'test-doors'])
                self.assertIn('2.500s', output.getvalue())
                self.assertIn('gmake test-doors', output.getvalue())

    def test_native_compiler_is_available_to_board_fault_helper(self):
        flags = ["gmake", "CC=/usr/gcc/15/bin/gcc", "CPPFLAGS=-D__EXTENSIONS__=1"]
        with patch.object(solaris_ci, "build_arguments", return_value=flags), \
             patch.dict(os.environ, {"CC": "cc"}):
            self.assertEqual(solaris_ci.configure_build(), flags)
            # The board fault subprocess reads CC from the test environment.
            self.assertEqual(os.environ["CC"], "/usr/gcc/15/bin/gcc")
            self.assertEqual(os.environ["CPPFLAGS"], "-D__EXTENSIONS__=1")

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
        self.assertEqual(native_ci.DEFAULT_TARGETS, ("freebsd", "rocky", "debian"))
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

    def test_native_schedule_is_daily_with_manual_override_and_same_lock(self):
        doc = yaml.safe_load((ROOT / ".github/workflows/solaris-build-test.yml").read_text())
        triggers = doc.get("on", doc.get(True))
        self.assertEqual(set(triggers), {"schedule", "workflow_dispatch"})
        self.assertEqual(triggers["schedule"], [{"cron": "23 19 * * *"}])
        self.assertEqual(doc["concurrency"], {"group": "liner-notes-native-solaris", "cancel-in-progress": False})
        self.assertIn("--target solaris", doc["jobs"]["test"]["steps"][1]["run"])

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

    def test_structured_scale_generator_uses_the_selected_make_toolchain(self):
        makefile=(ROOT/'Makefile').read_text()
        recipe=makefile.split('generate-structured: tests/generate_structured.c',1)[1].split('\n\n',1)[0]
        for flag in ('$(CC)','$(CPPFLAGS)','$(CFLAGS)','$(LDFLAGS)','$(SQLITE_CFLAGS)','$(SQLITE_LIBS)','$(LDLIBS)'):
            self.assertIn(flag,recipe)
        self.assertIn('test-stage70-scale: all generate-structured',makefile)
        self.assertIn('tests/test_stage70_scale.py ./runtime ./generate-structured',makefile)
        script=(ROOT/'tests/test_stage70_scale.py').read_text()
        self.assertNotIn("['cc'",script)
        self.assertIn("subprocess.run([generator,",script)

    def test_missing_tool_is_explicit_and_never_installs(self):
        with patch.object(solaris_ci.shutil, "which", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "Required installed tool missing"):
                solaris_ci.find_tool(["gmake"])

    def test_private_lrzsz_build_preserves_existing_os_defaults(self):
        for system, flag in (("freebsd15", "-U_POSIX_C_SOURCE"), ("linux", "-D_DEFAULT_SOURCE")):
            commands = build_lrzsz.build_commands("/private/local", system)
            self.assertEqual(commands, [
                ["./configure", "CC=cc", "CFLAGS=-O2 " + flag, "--prefix=/private/local"],
                ["make", "-j2"], ["make", "install"]])
        commands = build_lrzsz.build_commands("/private/local", "sunos5")
        self.assertIn("CC=gcc", commands[0])
        self.assertIn("CFLAGS=-O2 -m64 -D__EXTENSIONS__=1", commands[0])
        self.assertEqual(commands[1:], [["gmake", "-j2"], ["gmake", "install"]])
        explicit = build_lrzsz.build_commands("/private/local", "sunos5", "/usr/bin/gcc", "/usr/bin/gmake")
        self.assertIn("CC=/usr/bin/gcc", explicit[0])
        self.assertEqual(explicit[1], ["/usr/bin/gmake", "-j2"])


if __name__ == "__main__":
    unittest.main()
