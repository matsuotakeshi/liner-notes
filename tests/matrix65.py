"""Run independent regression cases, retaining every failure log."""
from pathlib import Path
import subprocess,shlex,json,time,os
root=Path(__file__).resolve().parents[1];os.chdir(root)
out=root/'evidence/stage65';out.mkdir(parents=True,exist_ok=True)
build=['make','-j3','runtime','vaultctl','memberctl','test-adapters','test-chat','test-vault-core','test-upload-core','test-transfer-connector','test-userlog','test-stage61-userlog','generate-scale','generate-members']
with (out/'build.log').open('w') as f:
 if subprocess.run(build,stdout=f,stderr=subprocess.STDOUT).returncode:raise SystemExit('build failed')
commands=[];active=False
targets={'test','test-stage61','test-maintenance','test-stage63','test-stage64'}
for line in (root/'Makefile').read_text().splitlines():
 if line and not line.startswith(('\t','#')):active=line.split(':')[0] in targets
 if active and line.startswith('\t'):
  command=line.strip()
  if '$(' not in command and command not in commands:commands.append(command)
commands+=['python3 tests/test_stage65.py ./runtime','python3 tests/test_stage65_doors.py ./runtime','python3 tests/test_stage65_terminal.py ./runtime','python3 tests/test_stage65_ui.py ./runtime','python3 tests/test_stage65_database.py ./runtime ./vaultctl']
results=[]
for i,command in enumerate(commands):
 name=f'{i:03d}.log';started=time.monotonic()
 try:
  with (out/name).open('w') as f:code=subprocess.run(shlex.split(command),stdout=f,stderr=subprocess.STDOUT,timeout=180).returncode
 except subprocess.TimeoutExpired:code='timeout'
 result={'command':command,'code':code,'seconds':round(time.monotonic()-started,2),'log':name};results.append(result)
 (out/'matrix.json').write_text(json.dumps(results,indent=2));print(result,flush=True)
(out/'matrix.done').write_text(str(sum(r['code']!=0 for r in results)))
