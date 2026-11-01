"""Exercise the SSH entry command under a real PTY, without editing sshd."""
import os
import pathlib
import subprocess
import sys
from test_telnet_client import run as native_client


def run(binary):
    if os.name!='posix':
        print('SKIP: SSH entry PTY check requires POSIX'); return
    entry=pathlib.Path(__file__).resolve().parents[1]/'scripts/liner-notes-ssh'
    for args in [['0'],['65536'],['-1'],['abc'],['2222;id'],['1','2'],['00022'],['9'*100]]:
        result=subprocess.run(['/bin/sh',str(entry)]+args,capture_output=True,timeout=5)
        assert result.returncode==2
    result=subprocess.run(['/bin/sh',str(entry),'2222'],capture_output=True,timeout=5)
    assert result.returncode==2 and b'ssh -t' in result.stderr
    native_client(binary,str(entry))
    print('PASS: SSH entry validates arguments, requires a PTY and provides a working BBS terminal without Telnet escapes')


if __name__=='__main__':
    run(sys.argv[1])
