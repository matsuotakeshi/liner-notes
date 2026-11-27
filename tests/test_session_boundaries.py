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
        expected = {'src/session.c', 'src/mmm_input.c', 'src/ktbbs_input.c', 'src/big_model_input.c','src/wani_bbs_input.c', 'src/net_cock_input.c', 'src/vs_input.c'} | {p.relative_to(ROOT).as_posix() for p in (ROOT/'src').glob('session_*.c') if not p.name.endswith('_core.c')}
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

    def test_control_effects_do_not_parse_display_payload(self):
        for p in (ROOT/'src').glob('session*.c'):
            text=p.read_text(encoding='utf-8')
            self.assertNotRegex(text,r'(?:strn?cmp|strstr|sscanf|check_traversal_response|list_continuation_parse|batch_response_ids|vault_children_response)\([^;\n]*completion->text')

    def test_operation_boundary_is_ui_independent(self):
        for name in ('session_core.h','session_core.c'):
            text=(ROOT/'src'/name).read_text(encoding='utf-8')
            self.assertNotRegex(text,r'#include "(?:ui|session|session_state|session_internal)\.h"')
            self.assertNotRegex(code(text),r'\b(?:Session|Ui|ui_format|session_line|board_submit)\b')
        text=(ROOT/'src/session.h').read_text(encoding='utf-8')
        self.assertIn('SessionCore core;',text)
        self.assertNotIn('session_state.h',text)
        self.assertNotIn('SessionAuth auth;',text)
        self.assertNotIn('SessionEditor editor;',text)
        self.assertIn('SessionUiBinding ui_binding;',text)
        state=(ROOT/'src/session_state.h').read_text(encoding='utf-8')
        self.assertIn('SessionAuth auth;',state)
        self.assertIn('SessionEditor editor;',state)
        self.assertNotIn('union',code(text))

    def test_core_contract_does_not_expose_worker_lifecycle(self):
        header=(ROOT/'src/session_core.h').read_text(encoding='utf-8')
        self.assertNotIn('#include "board.h"',header)
        self.assertIn('#include "operation.h"',header)
        contract=(ROOT/'src/operation.h').read_text(encoding='utf-8')
        self.assertNotRegex(code(contract),r'\bboard_(?:start|stop|submit|poll)\b')
        for p in (ROOT/'src').glob('session*.c'):
            if p.name=='session_core.c':continue
            text=code(p.read_text(encoding='utf-8'))
            self.assertNotRegex(text,r'->core\.(?:user|user_epoch|board|current|member_level)\s*=(?!=)',p.name)
            self.assertNotRegex(text,r'->core\.(?:board_grants|vault_grants)\[[^]]+\]\s*=',p.name)

    def test_ui_result_dispatch_is_after_core_receive(self):
        dispatcher=(ROOT/'src/session_ui.c').read_text(encoding='utf-8').split('void session_result(',1)[1].split('void session_impl_emit(',1)[0]
        self.assertLess(dispatcher.index('session_core_matches'),dispatcher.index('->keep_position'))
        self.assertLess(dispatcher.index('session_core_receive'),dispatcher.index('->result(s,'))
        for p in (ROOT/'src').glob('session*.c'):
            if p.name in ('session_core.c','session_ui.c'):continue
            self.assertNotRegex(code(p.read_text(encoding='utf-8')),r'\bsession_core_(?:complete|complete_mode|receive)\s*\(',p.name)
        session=(ROOT/'src/session.c').read_text(encoding='utf-8')
        self.assertIn('s->ui_binding.active->line(s,line,size)',session)
        self.assertNotIn('session_impl_normalize_command',session)
        make=(ROOT/'Makefile').read_text(encoding='utf-8')
        recipe=next(line for line in make.splitlines() if line.startswith('\t$(CC)') and ' -o test-adapters' in line)
        self.assertIn('tests/test_session_ui.c',recipe)

    def test_runtime_and_dispatcher_cannot_access_rtbbs_state(self):
        for name in ('runtime.c','session.c','session_ui.c'):
            text=code((ROOT/'src'/name).read_text(encoding='utf-8'))
            self.assertNotRegex(text,r'\brtbbs(?:_const)?\s*\(',name)
            self.assertNotRegex(text,r'(?:->|\.)(?:presentation|conversation|editor|reading|mail_state|files)\b',name)
            self.assertNotRegex(text,r'\bui_(?:text|format|prompt)\s*\(',name)
        for name in ('session_auth.c','session_chat.c'):
            text=(ROOT/'src'/name).read_text(encoding='utf-8')
            self.assertNotIn(',session_impl_chat_event,s,',text)
            self.assertIn(',session_chat_event,s,',text)

    def test_worker_audit_does_not_infer_events_from_display(self):
        text=(ROOT/'src/board.c').read_text(encoding='utf-8')
        self.assertNotRegex(text,r'(?:strn?cmp|strstr|sscanf)\(rs->text')
        self.assertIn('rs->feedback.article_access',text)

if __name__=='__main__':unittest.main()
