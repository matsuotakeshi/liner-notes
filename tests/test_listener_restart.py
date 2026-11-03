"""Real TCP restart and occupied-port regression; no BBS data or root needed."""
import os
from pathlib import Path
import socket
import subprocess
import sys
import threading


def start(binary, address, port):
    p=subprocess.Popen([binary,'--bind',address,str(port)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    lines=[]
    reader=threading.Thread(target=lambda:lines.append(p.stdout.readline()),daemon=True)
    reader.start();reader.join(10)
    if not lines or not lines[0].startswith('LISTEN '):
        p.kill();p.wait()
        raise AssertionError((address,port,lines,p.stderr.read()))
    return p,int(lines[0].split()[-1])


def stop(p):
    if p.poll() is None:p.terminate()
    try:p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill();p.wait();raise
    errors=p.stderr.read()
    assert p.returncode==0 and not errors,(p.returncode,errors)


def run(binary):
    binary=str(Path(binary).resolve())
    for address in ('127.0.0.1','0.0.0.0'):
        port=0
        for iteration in range(4):
            p,port=start(binary,address,port)
            try:
                if iteration==0:
                    collision=subprocess.run([binary,'--bind',address,str(port)],capture_output=True,text=True,timeout=10)
                    assert collision.returncode!=0,collision
                    assert f'listen setup failed: bind {address}:{port}:' in collision.stderr,collision.stderr
                    if os.name!='nt':assert 'errno=' in collision.stderr,collision.stderr
                with socket.create_connection(('127.0.0.1',port),timeout=5) as client:
                    assert client.recv(4096).startswith(b'RTBBS-PROBE ')
                    client.sendall(b'PING\r\n');assert client.recv(4096)==b'PONG\r\n'
                    # Server closes first. Client then acknowledges the FIN and
                    # closes, leaving the server-side connection in TIME_WAIT.
                    stop(p)
                    assert client.recv(4096)==b''
            finally:
                if p.poll() is None:stop(p)
        print(f'PASS listener {address}: occupied-port diagnostic and four immediate restarts after accepted TCP sessions')


if __name__=='__main__':run(sys.argv[1])
