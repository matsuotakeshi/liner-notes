"""Real resume checker can be rerun, and failed validation preserves its receipt."""
from pathlib import Path
import json,subprocess,sys,tempfile

receiver,sender=sys.argv[1:]
tool=Path(__file__).resolve().parents[1]/'ports/test-resume.py'
with tempfile.TemporaryDirectory(prefix='receipt71-') as tmp:
    receipt=Path(tmp)/'resume.json'
    for expected in ('created','unchanged'):
        result=subprocess.run([sys.executable,tool,receiver,sender,'--receipt',receipt],capture_output=True,text=True,timeout=45)
        assert result.returncode==0,(result.stdout,result.stderr)
        value=json.loads(result.stdout)
        assert value['receipt_status']==expected
        assert 0<value['sender_wire_bytes']<value['payload_bytes']
        print(json.dumps(value,sort_keys=True))
    before=receipt.read_bytes()
    result=subprocess.run([sys.executable,tool,receiver,'/usr/bin/false','--receipt',receipt],capture_output=True,text=True,timeout=45)
    assert result.returncode and receipt.read_bytes()==before
print('PASS real partial-file resume twice, duplicate receipt reuse, failed engine preserves validated receipt')
