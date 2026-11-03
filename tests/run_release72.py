"""Run release regressions in an isolated checkout; never operates on services."""
from pathlib import Path
import subprocess,sys,json,time,os
root=Path(__file__).resolve().parents[1]
out=root/'release-evidence';out.mkdir(exist_ok=True)
commands=[
 ['make','check'],
 ['make','test-stage67','test-stage68','test-stage69','test-stage70'],
 ['make','test-stage72'],
 [sys.executable,'tests/test_web_service.py'],
 [sys.executable,'tests/test_listener_restart.py','./runtime'],
]
env=dict(os.environ,LINER_TEST_START_TIMEOUT='180',PYTHON=sys.executable)
results=[]
for i,command in enumerate(commands):
 start=time.time();log=out/f'regression-{i+1}.log'
 with log.open('w') as f:
  code=subprocess.run(command,cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT).returncode
 results.append(dict(command=command,code=code,seconds=round(time.time()-start,2),log=log.name))
 (out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
 print(results[-1],flush=True)
 if code:raise SystemExit(code)
print('PASS release regressions',flush=True)
