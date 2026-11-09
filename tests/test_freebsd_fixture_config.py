"""Rootless package defaults never replace edited menus or use host assets."""
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'ci'))
from freebsd_fixture_config import prepare_assets


class FixtureConfigTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='pkg-menu-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.menus = self.root/'usr/local/share/liner-notes/menus'
        self.menus.mkdir(parents=True)
        for language in ('ja', 'en'):
            (self.menus/(language+'.txt.sample')).write_text(language+' menu', encoding='utf8')

    def test_fresh_install_and_updated_samples_preserve_edited_menu(self):
        paths = prepare_assets(self.root)
        for language in ('ja', 'en'):
            self.assertEqual((self.menus/(language+'.txt')).read_bytes(),
                             (self.menus/(language+'.txt.sample')).read_bytes())
        edited = self.menus/'ja.txt'
        edited.write_text('operator custom menu', encoding='utf8')
        for language in ('ja', 'en'):
            (self.menus/(language+'.txt.sample')).write_text('new package sample', encoding='utf8')
        self.assertEqual(prepare_assets(self.root), paths)
        self.assertEqual(edited.read_text(), 'operator custom menu')
        self.assertEqual((self.menus/'en.txt').read_text(), 'en menu')
        self.assertEqual(set(paths), {'liner_notes_banner','liner_notes_sysinfo','liner_notes_goodbye'})
        self.assertTrue(all(Path(p).is_relative_to(self.root) for p in paths.values()))

    def test_missing_sample_is_not_synthesized(self):
        (self.menus/'ja.txt.sample').unlink()
        with self.assertRaises(ValueError):
            prepare_assets(self.root)

    def test_existing_nonfile_is_rejected(self):
        (self.menus/'ja.txt').mkdir()
        with self.assertRaises(ValueError):
            prepare_assets(self.root)

    @unittest.skipUnless(os.name == 'posix', 'POSIX symlink fixture')
    def test_symlink_cannot_write_an_external_menu(self):
        outside = self.root/'outside'
        outside.write_text('unchanged')
        (self.menus/'ja.txt').symlink_to(outside)
        with self.assertRaises(ValueError):
            prepare_assets(self.root)
        self.assertEqual(outside.read_text(), 'unchanged')


if __name__ == '__main__':
    unittest.main()
