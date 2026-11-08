#!/usr/bin/env python3
"""Staging and SMF launch checks; never imports/enables a host SMF service."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import stat
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def load(name, source):
    spec = importlib.util.spec_from_file_location(name, ROOT / source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


installer = load("solaris_installer", "scripts/install-solaris.py")
service = load("solaris_service", "scripts/solaris_service.py")


class ServiceTests(unittest.TestCase):
    def test_disabled_contract_and_least_privilege_manifest(self):
        doc = ET.parse(ROOT / "solaris/liner-notes.xml")
        self.assertEqual(doc.find(".//create_default_instance").attrib["enabled"], "false")
        credential = doc.find(".//method_credential").attrib
        self.assertEqual(credential, dict(user="liner_notes", group="liner_notes", privileges="basic", limit_privileges="basic"))
        self.assertEqual(doc.find(".//exec_method[@name='stop']").attrib["exec"], ":kill")
        self.assertEqual(doc.find(".//propval[@name='duration']").attrib["value"], "contract")

    def test_array_arguments_and_invalid_config(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "launch.json"
            config = dict(program="/bin/true", working_directory="/var/opt/liner-notes", arguments=["literal; $(touch forbidden)"])
            path.write_text(json.dumps(config))
            info = list(path.stat())
            info[0] = stat.S_IFREG | 0o640
            with patch.object(service.os.path, "isfile", return_value=True), \
                 patch.object(service.os.path, "isdir", return_value=True), \
                 patch.object(Path, "stat", return_value=os.stat_result(info)), \
                 patch.object(service.os, "access", return_value=True):
                self.assertEqual(service.settings(path)["arguments"], config["arguments"])
                for bad in ("--menu; touch forbidden", [], [1], ["\0"], ["x" * 4097]):
                    path.write_text(json.dumps(dict(config, arguments=bad)))
                    with self.assertRaises(ValueError):
                        service.settings(path)

    def test_live_root_install_is_rejected(self):
        for dest in ("", Path.cwd().anchor):
            with self.assertRaises(ValueError):
                installer.install(dest)

    def test_stage_preserves_custom_config_and_has_no_service_side_effects(self):
        with tempfile.TemporaryDirectory() as tmp:
            stage = Path(tmp) / "stage"
            original = Path.read_bytes
            def binary(path):
                if path.parent == ROOT and path.name in ("runtime", "memberctl", "vaultctl", "dbconvert"):
                    return b"test executable\n"
                return original(path)
            with patch.object(Path, "read_bytes", binary), patch.object(subprocess, "run", side_effect=AssertionError("no host commands")):
                installer.install(str(stage))
                config = stage / "etc/liner-notes/solaris.json"
                sample = config.with_name("solaris.json.sample")
                self.assertEqual(json.loads(sample.read_text())["program"], "/opt/liner-notes/sbin/liner-notes")
                config.write_text("custom settings\n")
                installer.install(str(stage))
                self.assertEqual(config.read_text(), "custom settings\n")
                self.assertFalse((stage / "var").exists())
                self.assertFalse((stage / "lib/svc/manifest").exists())

    @unittest.skipUnless(os.name == "posix", "Unix file permissions")
    def test_writable_config_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp) / "settings.json"
            p.write_text("{}")
            p.chmod(0o666)
            with self.assertRaisesRegex(ValueError, "writable"):
                service.settings(p)

    def test_start_does_not_use_shell_or_detach_contract(self):
        from unittest.mock import Mock
        child = Mock()
        child.poll.return_value = None
        config = dict(program="/bin/bbs", working_directory="/data", arguments=["literal;word"])
        with patch.object(service.subprocess, "Popen", return_value=child) as spawn, \
             patch.object(service.time, "sleep"), patch.object(service.os, "umask"):
            service.start(config)
        spawn.assert_called_once_with(["/bin/bbs", "literal;word"], cwd="/data", stdin=subprocess.DEVNULL, close_fds=True)
        child.poll.return_value = 1
        with patch.object(service.subprocess, "Popen", return_value=child), \
             patch.object(service.time, "sleep"), patch.object(service.os, "umask"):
            with self.assertRaisesRegex(RuntimeError, "startup"):
                service.start(config)


if __name__ == "__main__":
    unittest.main()
