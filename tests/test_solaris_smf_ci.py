"""SMF fixture boundaries; no host service or privilege operations."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ci"))
import solaris_smf_control as control
import solaris_smf_lifecycle as lifecycle
import prepare_solaris_smf as prepare
import solaris_smf_method as method


class Fake:
    def __init__(self, present=False):
        self.present = present
        self.state = "disabled"
        self.enabled = "false"
        self.commands = []

    def __call__(self, args, check=True):
        from types import SimpleNamespace
        self.commands.append(args)
        if args[0] == control.SVCS:
            return SimpleNamespace(returncode=0 if self.present else 1,
                                   stdout=self.state if args[-2] == "state" else control.FMRI)
        if args[0] == control.SVCPROP:
            return SimpleNamespace(returncode=0, stdout=self.enabled)
        if args[0] == control.SVCCFG:
            if args[1] == "list":
                return SimpleNamespace(returncode=0, stderr="",
                                       stdout="application/liner-notes-ci\n" if self.present else "")
            if args[1] == "import": self.present = True
            if args[1] == "delete": self.present = False
        if args[0] == control.SVCADM:
            if args[1] == "enable": self.state, self.enabled = "online", "true"
            if args[1] == "disable": self.state, self.enabled = "disabled", "false"
        return SimpleNamespace(returncode=0, stdout="")


class SmfTests(unittest.TestCase):
    def test_manifest_preserves_contract_and_nonroot_identity(self):
        doc = ET.fromstring(prepare.manifest())
        service = doc.find("service")
        self.assertEqual(service.attrib["name"], "application/liner-notes-ci")
        self.assertEqual(service.find("create_default_instance").attrib["enabled"], "false")
        self.assertEqual(service.find("method_context/method_credential").attrib,
                         dict(user="ci-test", group="staff", privileges="basic", limit_privileges="basic"))
        self.assertEqual(service.find("exec_method[@name='stop']").attrib["exec"], ":kill")
        self.assertEqual(service.find("exec_method[@name='start']").attrib["exec"],
                         "/usr/bin/python3.13 -I /opt/liner-notes-ci/solaris_smf_method.py")
        self.assertFalse(doc.findall(".//propval[@name='modify_authorization']"))
        self.assertFalse(doc.findall(".//propval[@name='value_authorization']"))

    def test_prepare_only_generates_new_files(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(control.subprocess, "run", side_effect=AssertionError("no host commands")):
            dest = prepare.prepare(Path(tmp) / "bundle")
            self.assertEqual(len(json.loads((dest / "SHA256.json").read_text())), 5)
            sudo = (dest / "liner-notes-smf-ci.sudoers").read_text()
            self.assertEqual(sudo, "ci-test ALL=(root) NOPASSWD: /opt/liner-notes-ci/solaris_smf_control.py session\n")
            with self.assertRaises(FileExistsError): prepare.prepare(dest)

    def test_other_service_and_arbitrary_commands_rejected(self):
        with patch.object(control, "command", side_effect=AssertionError("must not execute")):
            for action in ("reboot", "enable -r", "enable svc:/network/ssh:default", "kill 1", "status /etc/shadow", "import", "delete", "enable\nrestart"):
                with self.subTest(action=action), self.assertRaises(ValueError):
                    control.act(action)

    def test_fixed_actions_never_accept_targets_or_recursive_enable(self):
        fake = Fake(True)
        with patch.object(control, "command", fake):
            for action in ("enable", "disable", "restart", "clear", "status", "ping"):
                control.act(action)
        for command in fake.commands:
            self.assertEqual(command[-1], control.FMRI)
            self.assertNotIn("-r", command)

    def test_preexisting_service_is_not_adopted_or_modified(self):
        fake = Fake(True)
        with patch.object(control, "command", fake), self.assertRaisesRegex(ValueError, "already exists"):
            control.serve()
        self.assertEqual(fake.commands, [[control.SVCCFG, "list", "application/liner-notes-ci"]])

    def test_repository_errors_never_mean_absent(self):
        for result in (SimpleNamespace(returncode=1, stdout="", stderr="fatal"),
                       SimpleNamespace(returncode=2, stdout="", stderr="bad option"),
                       SimpleNamespace(returncode=0, stdout="", stderr="warning"),
                       SimpleNamespace(returncode=0, stdout="other/service\n", stderr="")):
            with self.subTest(result=result), patch.object(control, "command", return_value=result) as call, \
                 self.assertRaises(RuntimeError):
                control.serve()
            self.assertEqual(call.call_count, 1)  # no validate/import/delete

    @unittest.skipUnless(os.name == "posix", "POSIX pipe/select")
    def test_finish_eof_and_invalid_request_all_clean_owned_service(self):
        for wire, error in ((b"enable\nstatus\nfinish\n", None), (b"enable\n", None),
                            (b"enable svc:/network/ssh:default\n", ValueError), (b"x" * 257, ValueError)):
            with self.subTest(wire=wire[:30]):
                read, write = os.pipe()
                os.write(write, wire); os.close(write)
                fake = Fake()
                try:
                    with patch.object(control, "command", fake), patch.object(control, "send"):
                        if error:
                            with self.assertRaises(error): control.serve(read)
                        else: control.serve(read)
                finally: os.close(read)
                self.assertFalse(fake.present)
                self.assertIn([control.SVCADM, "disable", "-s", "-T", "30", control.FMRI], fake.commands)
                self.assertIn([control.SVCCFG, "delete", control.SERVICE], fake.commands)

    def test_lost_heartbeat_cleans_without_accepting_commands(self):
        fake = Fake()
        with patch.object(control, "command", fake), patch.object(control, "send"), \
             self.assertRaises(TimeoutError):
            control.serve(clock=iter((0, 0, 31, 31)).__next__)
        self.assertFalse(fake.present)

    def test_partial_import_failure_still_cleans_owned_service(self):
        fake = Fake()
        def partial(args, check=True):
            result = fake(args, check)
            if args[:2] == [control.SVCCFG, "import"]:
                raise RuntimeError("partial import")
            return result
        with patch.object(control, "command", partial), self.assertRaisesRegex(RuntimeError, "partial import"):
            control.serve()
        self.assertFalse(fake.present)
        self.assertIn([control.SVCCFG, "delete", control.SERVICE], fake.commands)

    def test_disable_failure_preserves_service_for_administrator_review(self):
        fake = Fake()
        def failure(args, check=True):
            result = fake(args, check)
            if args[:2] == [control.SVCADM, "disable"]:
                raise RuntimeError("disable failed")
            return result
        with patch.object(control, "command", failure), patch.object(control, "send"), \
             self.assertRaisesRegex(RuntimeError, "disable failed"):
            control.serve(clock=iter((0, 0, 31, 31)).__next__)
        self.assertTrue(fake.present)
        self.assertNotIn([control.SVCCFG, "delete", control.SERVICE], fake.commands)

    def test_method_rejects_root_other_uid_and_other_service(self):
        fakepwd = SimpleNamespace(getpwnam=lambda name: SimpleNamespace(pw_uid=1234))
        with patch.dict(sys.modules, {"pwd": fakepwd}), patch.dict(os.environ, {"SMF_FMRI": control.FMRI}):
            for uid in (0, 1235):
                with self.subTest(uid=uid), patch.object(method.os, "geteuid", return_value=uid, create=True), \
                     self.assertRaisesRegex(ValueError, "Nonprivileged"):
                    method.selection()
            with patch.object(method.os, "geteuid", return_value=1234, create=True), \
                 patch.dict(os.environ, {"SMF_FMRI": "svc:/network/ssh:default"}), \
                 self.assertRaisesRegex(ValueError, "Dedicated SMF"):
                method.selection()

    @unittest.skipUnless(os.name == "posix", "POSIX private ownership and paths")
    def test_method_selection_boundaries(self):
        uid = os.geteuid()
        if uid == 0:
            self.skipTest("requires ordinary hosted account")
        fakepwd = SimpleNamespace(getpwnam=lambda name: SimpleNamespace(pw_uid=uid))
        root = Path("/export/home/ci-test/ci/native-solaris-test/source")
        good = {"source": str(root), "sha": "a" * 40,
                "config": str(root / "build/smf-lifecycle/launch.json")}
        original_read = Path.read_text
        def read(path, *args, **kwargs):
            if path == root / ".ci-source-sha": return "a" * 40
            return original_read(path, *args, **kwargs)
        with tempfile.TemporaryDirectory() as tmp, patch.dict(sys.modules, {"pwd": fakepwd}), \
             patch.dict(os.environ, {"SMF_FMRI": control.FMRI}), patch.object(method, "AREA", Path(tmp)), \
             patch.object(Path, "read_text", read):
            area = Path(tmp); area.chmod(0o700)
            target = area / "selection.json"
            def reset(value=good):
                target.write_text(json.dumps(value)); target.chmod(0o600)
            reset()
            self.assertEqual(method.selection(), (root, Path(good["config"])))
            for changes in ({"source": "/tmp/source"}, {"sha": "b" * 40},
                            {"config": "/etc/liner-notes/solaris.json"}, {"extra": "field"}):
                reset({**good, **changes})
                with self.subTest(changes=changes), self.assertRaises(ValueError): method.selection()
            reset(); target.chmod(0o644)
            with self.assertRaisesRegex(ValueError, "Private CI"): method.selection()
            target.chmod(0o600); area.chmod(0o755)
            with self.assertRaisesRegex(ValueError, "Private CI"): method.selection()
            area.chmod(0o700)
            saved = area / "saved.json"; target.rename(saved); target.symlink_to(saved)
            with self.assertRaisesRegex(ValueError, "Private CI"): method.selection()

    @unittest.skipUnless(os.name == "posix", "POSIX pipe/select")
    def test_partial_line_cannot_block_heartbeat_deadline(self):
        read, write = os.pipe()
        os.write(write, b"en")  # leave writer open: readline would block here
        fake = Fake()
        try:
            with patch.object(control, "command", fake), patch.object(control, "send"), \
                 self.assertRaises(TimeoutError):
                control.serve(read, clock=iter((0, 0, 0, 0, 0, 31)).__next__)
        finally:
            os.close(write); os.close(read)
        self.assertFalse(fake.present)

    def test_client_rejects_arbitrary_actions_before_pipe(self):
        instance = object.__new__(lifecycle.Session)
        for action in ("import", "reboot", "disable ssh", "log /etc/shadow"):
            with self.assertRaises(ValueError): instance.request(action)

    @unittest.skipUnless(os.name == "posix", "POSIX nonblocking pipe")
    def test_blocked_reply_writer_expires_and_restores_fd_mode(self):
        read, write = os.pipe()
        try:
            os.set_blocking(write, False)
            while True:
                try: os.write(write, b"x" * 4096)
                except BlockingIOError: break
            while True:
                try: os.write(write, b"x")
                except BlockingIOError: break
            os.set_blocking(write, True)
            with self.assertRaisesRegex(TimeoutError, "reader stalled"):
                control.send({"alive": True}, write, clock=iter((0, 0, 11)).__next__)
            self.assertTrue(os.get_blocking(write))
        finally:
            os.close(read); os.close(write)

    def test_reply_failure_cleans_owned_service(self):
        fake = Fake()
        with patch.object(control, "command", fake), \
             patch.object(control, "send", side_effect=TimeoutError("reply reader stalled")), \
             self.assertRaises(TimeoutError):
            control.serve()
        self.assertFalse(fake.present)


if __name__ == "__main__":
    unittest.main()
