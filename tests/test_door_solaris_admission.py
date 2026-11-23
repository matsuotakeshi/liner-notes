"""Check the Solaris launch gate without starting a privileged process."""

import os
from pathlib import Path
import stat
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, call, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "solaris"), str(ROOT / "scripts")]
import door_solaris_admission as admission

EXEC_ROOT = "/var/db/liner-notes/door-root"


def mount(point=EXEC_ROOT, options="rw,nosetuid,nodevices,dev=1", source="swap", kind="tmpfs"):
    return f"{source}\t{point}\t{kind}\t{options}\t0\n"


class SolarisAdmission(unittest.TestCase):
    def test_root_must_have_canonical_absolute_spelling(self):
        self.assertEqual(admission._root_path(EXEC_ROOT), EXEC_ROOT)
        for path in ("/", "//", "//root", "/a/../b", "/a/./b", "/root/", "root", "/door root"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                admission._root_path(path)

    def test_mount_requires_exact_nosuid_root_without_submounts(self):
        admission._verify_mount(mount(), EXEC_ROOT)
        admission._verify_mount(mount(options="nosuid") + mount(point=EXEC_ROOT + "-other"), EXEC_ROOT)
        for table in (
            "",
            "incomplete line\n",
            mount(point="/var/db/liner-notes"),
            mount(source="unexpected"),
            mount(kind="lofs"),
            mount(options="nosetuid"),
            mount(options="nosuid,devices"),
            mount(options="nosuid,setuid"),
            mount(options="nosuid,noexec"),
            mount() + mount(),
            mount() + mount(point=EXEC_ROOT + "/system/contract", kind="ctfs"),
            mount() + mount(point=EXEC_ROOT + "/data directory"),
        ):
            with self.subTest(table=table), self.assertRaises(ValueError):
                admission._verify_mount(table, EXEC_ROOT)

    def test_open_descriptor_must_match_the_checked_mount(self):
        identity = SimpleNamespace(st_mode=stat.S_IFDIR | 0o755, st_uid=0, st_dev=17, st_ino=23)
        opened = dict(vars(identity))
        system = Mock(path=os.path, ST_NOSUID=2)
        system.fstat.return_value = identity
        system.fstatvfs.return_value = SimpleNamespace(f_flag=2)
        system.stat.return_value = identity
        with patch.object(admission, "os", system), patch.object(admission, "Path") as path:
            path.return_value.read_text.return_value = mount()
            admission._verify_root(EXEC_ROOT, 9)
            system.fstatvfs.assert_called_once_with(9)
            system.stat.assert_called_once_with(EXEC_ROOT, follow_symlinks=False)
            system.stat.return_value = SimpleNamespace(**(opened | {"st_dev": 19}))
            with self.assertRaisesRegex(ValueError, "changed during admission"):
                admission._verify_root(EXEC_ROOT, 9)
            system.fstatvfs.return_value.f_flag = 0
            with self.assertRaisesRegex(ValueError, "missing nosuid"):
                admission._verify_root(EXEC_ROOT, 9)
            system.fstat.return_value = SimpleNamespace(**(opened | {"st_uid": 61001}))
            with self.assertRaisesRegex(ValueError, "Unsafe"):
                admission._verify_root(EXEC_ROOT, 9)

    def test_refusal_precedes_a_slot_reservation(self):
        system = Mock(
            fspath=os.fspath, O_RDONLY=0, O_DIRECTORY=1, O_NOFOLLOW=2, O_CLOEXEC=4
        )
        system.getuid.return_value = system.geteuid.return_value = 0
        system.open.return_value = 9
        with (
            patch.object(admission, "os", system),
            patch.object(admission, "sys", SimpleNamespace(platform="sunos5")),
            patch.object(admission, "secure_path") as secure,
            patch.object(admission, "_verify_root", side_effect=ValueError("missing nosuid")),
            patch.object(admission, "reserve") as reserve,
            self.assertRaisesRegex(ValueError, "missing nosuid"),
            admission.admit(EXEC_ROOT, "/state", 61001),
        ):
            self.fail("admitted an unverified root")
        secure.assert_called_once_with(EXEC_ROOT, 0, directory=True)
        system.open.assert_called_once_with(EXEC_ROOT, 7)
        system.close.assert_called_once_with(9)
        reserve.assert_not_called()

    def test_interrupted_launch_closes_handles_without_releasing_the_slot(self):
        ledger = Mock()
        system = ledger.system
        system.fspath = os.fspath
        system.O_RDONLY, system.O_DIRECTORY, system.O_NOFOLLOW, system.O_CLOEXEC = 0, 1, 2, 4
        system.getuid.return_value = system.geteuid.return_value = 0
        system.open.return_value = 9
        with (
            patch.object(admission, "os", system),
            patch.object(admission, "sys", SimpleNamespace(platform="sunos5")),
            patch.object(admission, "secure_path", ledger.secure),
            patch.object(admission, "_verify_root", ledger.verify),
            patch.object(admission, "reserve", ledger.reserve),
            self.assertRaisesRegex(RuntimeError, "startup interrupted"),
        ):
            with admission.admit(EXEC_ROOT, "/state", 61001) as (fd, reservation):
                self.assertEqual(fd, 9)
                self.assertIs(reservation, ledger.reserve.return_value)
                raise RuntimeError("startup interrupted")
        ledger.reserve.return_value.stop_and_release.assert_not_called()
        self.assertEqual(ledger.mock_calls[-4:], [
            call.verify(EXEC_ROOT, 9),
            call.reserve("/state", 61001),
            call.reserve().close(),
            call.system.close(9),
        ])

    @unittest.skipUnless(sys.platform.startswith("sunos"), "live Solaris mount table")
    def test_native_mount_table_rejects_an_unmounted_subdirectory(self):
        table = Path("/etc/mnttab").read_text(encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "needs nosuid and nodevices"):
            admission._verify_mount(table, "/var/db/liner-notes/no-execution-root")


if __name__ == "__main__":
    unittest.main()
