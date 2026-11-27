"""Run the export inventory on synthetic files; verify auxiliary data byte for byte."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import database_tools


class CopyInventory(unittest.TestCase):
    def test_only_authoritative_database_files_are_excluded(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / 'data'
            (root / 'db').mkdir(parents=True)
            db = 'db/station.sqlite'
            excluded = {db + suffix for suffix in ('', '.lock', '-wal', '-shm', '-journal')}
            retained = {db + '-operator', db + '.lock-notes', db + '-wal.previous',
                        'db/other.sqlite-wal', 'station.sqlite-wal'}
            for name in excluded | retained:
                (root / name).write_bytes(name.encode())
            copy = Path(folder) / 'copy'
            database_tools.copy_instance(root, copy, excluded)
            self.assertEqual({p.relative_to(copy).as_posix() for p in copy.rglob('*') if p.is_file()}, retained)
            for name in retained:
                self.assertEqual((copy / name).read_bytes(), name.encode())


if __name__ == '__main__':
    unittest.main()
