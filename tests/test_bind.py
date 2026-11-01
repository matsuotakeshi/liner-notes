"""Explicit external IPv4 binding without changing the default listener."""
import pathlib
import socket
import subprocess
import sys
import tempfile
from test_board import Server
from test_telnet import Client


def run(binary, host=None):
    binary = str(pathlib.Path(binary).resolve())
    with tempfile.TemporaryDirectory(prefix='bind-') as d:
        p = pathlib.Path(d)
        for address in ['', 'localhost', '::1', '256.0.0.1', '127.0.0.1:2222', '0.0.0.0 extra']:
            path = p/'invalid'
            result = subprocess.run([binary,'--bind',address,'0',str(path)],capture_output=True,timeout=5)
            assert result.returncode and b'--bind' in result.stderr and not path.exists()
        addresses = ['127.0.0.1','0.0.0.0'] + ([host] if host else [])
        for address in addresses:
            s=Server(binary,p/'data',members=p/'users',menu=True,telnet=True,language='en',bind=address)
            c=Client(s.port,host=host if host and address!='127.0.0.1' else '127.0.0.1')
            try:
                assert b'Liner notes' in c.until()
                assert b'PONG' in c.cmd('PING')
                c.cmd('Q',b'> '); assert b'Goodbye' in c.cmd('Y',b'Goodbye.')
            finally:
                c.close();s.close()
        # Wildcard listening remains exclusive; a second listener must fail.
        s=Server(binary,p/'data',bind='0.0.0.0')
        try:
            result=subprocess.run([binary,'--bind','0.0.0.0',str(s.port)],capture_output=True,timeout=5)
            assert result.returncode and b'listen setup failed' in result.stderr
        finally:
            s.close()
        print('PASS: explicit loopback/wildcard binding, Telnet menus, invalid address rejection and bind conflict')
        if host:
            print('PASS: specific LAN address and wildcard reached through the LAN address')


if __name__=='__main__':
    run(sys.argv[1],sys.argv[2] if len(sys.argv)>2 else None)
