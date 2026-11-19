"""Architecture checks supplement executable Session regressions; no OS contacts."""
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[1]

def code(text):
    return re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', ' ', text)

class SessionBoundaries(unittest.TestCase):
    def test_all_adapters_are_separate_build_inputs(self):
        make = (ROOT/'Makefile').read_text(encoding='utf-8')
        sources = re.search(r'^SESSION_SOURCES= (.+)$', make, re.M).group(1).split()
        expected = {'src/session.c'} | {p.relative_to(ROOT).as_posix() for p in (ROOT/'src').glob('session_*.c') if not p.name.endswith('_core.c')}
        self.assertEqual(set(sources), expected)
        self.assertEqual(len(sources),len(set(sources)))
        for target in ('runtime', 'test-adapters'):
            rule = next(line for line in make.splitlines() if line.startswith(target+':') and '$(SESSION_SOURCES)' in line)
            self.assertIn('$(SESSION_SOURCES)', rule)
            recipe = next(line for line in make.splitlines() if line.startswith('\t$(CC)') and ' -o '+target in line)
            self.assertIn('$(SESSION_SOURCES)', recipe)
        for name in sources:
            text = (ROOT/name).read_text(encoding='utf-8')
            self.assertFalse(re.search(r'^\s*#\s*include\s+"[^"]+\.(?:c|inc)"',text,re.M),name)

    def test_private_interface_stays_inside_adapters(self):
        for p in (ROOT/'src').glob('*'):
            if p.suffix not in ('.h','.c','.inc'):continue
            text = p.read_text(encoding='utf-8')
            if '#include "session_internal.h"' in text:
                self.assertTrue(p.name=='session.c' or (p.name.startswith('session_') and not p.name.endswith('_core.c')),p.name)
            if p.name!='session.c' and (p.name.startswith('session_') and p.suffix=='.c'):
                self.assertNotRegex(code(text),r'\bboard_submit\s*\(')
        self.assertNotIn('session_internal', (ROOT/'src/session.h').read_text(encoding='utf-8'))

    def test_core_policy_has_no_adapter_or_io_dependencies(self):
        for name in ('session_policy_core.c','session_policy_core.h'):
            text=(ROOT/'src'/name).read_text(encoding='utf-8')
            quoted=re.findall(r'#include "([^"]+)"',text)
            self.assertTrue(set(quoted)<= {'session_policy_core.h'},name)
            self.assertNotRegex(code(text),r'\b(?:Session|BoardRequest|Ui|socket|connect|fopen|sqlite3_open|board_submit)\b')

if __name__=='__main__':unittest.main()
