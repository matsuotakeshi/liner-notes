"""Idle watchdogs permit ongoing sends/receives and stop silent peers."""
import os,sys,tempfile,threading,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from zmodem_connector import ZmodemProcess,TransferFailure
from legacy_transfer import Wire

def run():
 with tempfile.TemporaryDirectory() as tmp:
  p=Path(tmp);engine=p/'engine'
  engine.write_text('#!'+sys.executable+'\n'+'''
import sys,time,os
from pathlib import Path
if '--version' in sys.argv:print('lsz (GNU lrzsz) 0.13.1');raise SystemExit(0)
if '--stall' in sys.argv:time.sleep(3);raise SystemExit(0)
for i in range(9):os.write(1,b'x');time.sleep(.2)
Path(sys.argv[sys.argv.index('--journal')+1]).write_text('engine proto=z filename=file status=ok \\n')
''');engine.chmod(0o700)
  # Actual child processes and OS pipes exercise the same relay as lrzsz.
  for stalled in (False,True):
   r,w=os.pipe();out,send=os.pipe();data=bytearray()
   def drain():
    while chunk:=os.read(out,4096):data.extend(chunk)
   t=threading.Thread(target=drain);t.start();start=time.monotonic()
   try:
    z=ZmodemProcess(sender=str(engine),timeout=1)
    try:z._run(str(engine),['--stall'] if stalled else [],p,r,send,lambda:None,False)
    except TransferFailure as error:assert stalled and error.code=='TIMEOUT',error
    else:assert not stalled
   finally:
    for fd in (r,w,send):os.close(fd)
    t.join();os.close(out)
   if not stalled:assert bytes(data)==b'x'*9 and time.monotonic()-start>1
  r,w=os.pipe()
  def produce():
   for _ in range(8):os.write(w,b'y');time.sleep(.2)
  t=threading.Thread(target=produce);t.start();wire=Wire(r,w,1,lambda:None)
  assert wire.read(8)==b'y'*8;t.join()
  try:wire.read(1);raise AssertionError('silent peer accepted')
  except TransferFailure as error:assert error.code=='TIMEOUT'
  os.close(r);os.close(w)
 print('PASS common idle timeout: active process send and legacy receive exceed total timeout; stalled peers fail TIMEOUT')
if __name__=='__main__':run()
