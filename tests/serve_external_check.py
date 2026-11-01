"""Bounded temporary LAN/SSH smoke-test server; never uses operational data."""
import pathlib,sys,tempfile,time
from test_board import Server
root=pathlib.Path(__file__).resolve().parents[1]
stop=root/'external-check.stop'
if stop.exists(): raise SystemExit('stale stop marker: choose a fresh check directory')
with tempfile.TemporaryDirectory(prefix='liner-external-') as d:
    p=pathlib.Path(d)
    s=Server(str(pathlib.Path(sys.argv[1]).resolve()),p/'data',members=p/'members',telnet=True,menu=True,language='en',bind='0.0.0.0')
    try:
        print('EXTERNAL_CHECK_PORT',s.port,flush=True)
        end=time.monotonic()+600
        while time.monotonic()<end and not stop.exists():time.sleep(.2)
    finally:s.close()
print('EXTERNAL_CHECK_STOPPED',flush=True)
