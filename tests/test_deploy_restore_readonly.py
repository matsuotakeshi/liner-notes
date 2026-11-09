#!/usr/bin/env python3
"""Offline SQLite restore checks must not create locks/SHM on original data."""
import hashlib
import os
from pathlib import Path
import shutil
import sqlite3
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import deploy_restore as restore


class ReadOnlyRestoreTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='deploy-readonly-')
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.database = self.source / 'database.sqlite'

    def tearDown(self):
        self.temporary.cleanup()

    def create(self, wal=False):
        # Abrupt process exit keeps committed WAL frames without checkpointing.
        code = ("import os,sqlite3,sys;d=sqlite3.connect(sys.argv[1]);"
                + ("d.execute('PRAGMA journal_mode=WAL');" if wal else '')
                + "d.execute('CREATE TABLE records(n INTEGER,body TEXT)');"
                "d.execute(\"INSERT INTO records VALUES (7,'committed WAL content')\");"
                "d.commit();os._exit(0)")
        subprocess.run([sys.executable, '-c', code, str(self.database)], check=True)
        if wal:
            Path(str(self.database) + '-shm').unlink()

    def state(self):
        return {path.name: (hashlib.sha256(path.read_bytes()).hexdigest(),
                            stat.S_IMODE(path.stat().st_mode), path.stat().st_mtime_ns,
                            path.stat().st_ino)
                for path in self.source.iterdir() if path.is_file()}

    def test_wal_without_shm_is_read_only_and_included(self):
        self.create(wal=True)
        before = self.state()
        connect = sqlite3.connect
        seen = []
        def copied_connection(database_uri, **kwargs):
            self.assertNotIn(self.source.as_uri(), database_uri)
            seen.append(database_uri)
            return connect(database_uri, **kwargs)
        with patch.object(restore.sqlite3, 'connect', side_effect=copied_connection):
            self.assertEqual(restore.snapshot_rows(self.database, 'SELECT * FROM records'),
                             [(7, 'committed WAL content')])
        self.assertEqual(len(seen), 1)
        self.assertEqual(before, self.state())
        self.assertFalse(Path(str(self.database) + '-shm').exists())

    def test_regular_database_is_unchanged(self):
        self.create()
        before = self.state()
        self.assertEqual(restore.snapshot_rows(self.database, 'SELECT COUNT(*) FROM records'), [(1,)])
        self.assertEqual(before, self.state())

    def test_rollback_journal_is_not_recovered(self):
        self.create()
        Path(str(self.database) + '-journal').write_bytes(b'nonempty interrupted transaction')
        before = self.state()
        with self.assertRaisesRegex(ValueError, 'rollback journal'):
            restore.snapshot_rows(self.database, 'SELECT * FROM records')
        self.assertEqual(before, self.state())

    def test_nonselect_is_rejected_without_opening_sqlite(self):
        self.create()
        before = self.state()
        with patch.object(restore.sqlite3, 'connect') as connect:
            with self.assertRaises(ValueError):
                restore.snapshot_rows(self.database, 'DELETE FROM records')
            connect.assert_not_called()
        self.assertEqual(before, self.state())

    def test_corrupt_database_never_changes_original(self):
        self.database.write_bytes(b'not a SQLite database')
        before = self.state()
        with self.assertRaises(sqlite3.DatabaseError):
            restore.snapshot_rows(self.database, 'SELECT * FROM records')
        self.assertEqual(before, self.state())

    def test_change_during_query_is_rejected(self):
        self.create()
        connect = sqlite3.connect
        def change_after_copy(database_uri, **kwargs):
            with self.database.open('ab') as stream:
                stream.write(b'changed by simulated external writer')
            return connect(database_uri, **kwargs)
        with patch.object(restore.sqlite3, 'connect', side_effect=change_after_copy):
            with self.assertRaisesRegex(ValueError, 'changed during verification'):
                restore.snapshot_rows(self.database, 'SELECT * FROM records')

    @unittest.skipIf(os.name == 'nt', 'UNIX no-follow behavior is tested by hosted CI')
    def test_symlink_replacement_is_not_followed(self):
        self.create()
        protected = self.root / 'protected'
        protected.write_bytes(b'never opened')
        real_open = os.open
        def swap(path, flags, *args, **kwargs):
            if Path(path) == self.database:
                self.database.unlink()
                self.database.symlink_to(protected)
            return real_open(path, flags, *args, **kwargs)
        with patch.object(restore.os, 'open', side_effect=swap):
            with self.assertRaises(OSError):
                restore.snapshot_rows(self.database, 'SELECT * FROM records')
        self.assertEqual(protected.read_bytes(), b'never opened')

    @unittest.skipIf(os.name == 'nt', 'UNIX absolute backup layout')
    def test_complete_restore_check_keeps_original_wal_inventory(self):
        self.create(wal=True)
        before = self.state()
        restored = self.root / 'restored'
        copy = restored / str(self.source).lstrip('/')
        copy.parent.mkdir(parents=True)
        shutil.copytree(self.source, copy)
        restore.verify_restored(restored, [str(self.source)],
                                [{'path': str(self.database), 'query': 'SELECT * FROM records'}])
        self.assertEqual(before, self.state())
        self.assertFalse(Path(str(copy / self.database.name) + '-shm').exists())


if __name__ == '__main__':
    unittest.main()
