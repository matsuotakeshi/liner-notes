"""Presentation records must not reinterpret article/private/user content."""
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from webui.reports import parse_report
from webui.bridge import response_data
from webui.commands import COMMANDS,visible,build_action

class Reports(unittest.TestCase):
 def test_reports(self):
  report=parse_report('CONNECTIONS\r\nCONNECTION 1 alice BBS TELNET 127.0.0.1:22 1 0 20 40 0 80 24\r\nEND CONNECTIONS\r\n')
  self.assertEqual(report['rows'][0][1],'alice');self.assertEqual(len(report['columns']),12)
  report=parse_report('USERLOG\r\nUSERROW 1 2 3 L 1700000000 15\r\nEND NEXT 4\r\n')
  self.assertEqual(report['next'],4)
  report=parse_report('FILEAUDIT\r\nVEVENT 1 2 2026-11-04T00:00:00Z E\talice\t<script>\tnew\r\nEND NEXT 0\r\n')
  self.assertEqual(report['rows'][0][-2],'<script>') # DOM textContent, not HTML.
  self.assertIsNone(parse_report('ARTICLE 1 PARENT 0\r\nTITLE test\r\nBODY 20\r\nCONNECTIONS\r\nCONNECTION 1 user\r\n'))
  self.assertIsNone(parse_report('CONNECTIONS\r\nCONNECTION broken\r\n'))
  self.assertEqual(response_data('MEMBERS\r\nMEMBER 2 bob active\r\nEND NEXT 0\r\n')['report']['rows'],[['2','bob','active']])
 def test_action_coverage(self):
  admin={'user':1,'admin':True};member={'user':2};guest={'user':0}
  for key,item in COMMANDS.items():
   self.assertTrue(item['label']);self.assertIn(item['group'],{'board','mail','files','chat','gather','people','settings','help','games','admin'})
   if item['role']=='admin':self.assertFalse(visible(item,member));self.assertFalse(visible(item,guest))
   self.assertTrue(visible(item,admin),key)
   # Every declared action has a graphical form definition; no raw command
   # substitution is needed to execute its fields with explicit confirmation.
   args={f['name']:f['default'] or '1' for f in item['fields']}
   command,_=build_action(key,args,admin,True);self.assertTrue(command)

if __name__=='__main__':unittest.main()
