#!/usr/bin/env python3
"""Bounded Telnet/RAW vault download. Use an SSH tunnel on untrusted networks."""
import argparse
import base64
import getpass
import hashlib
import os
import pathlib
import re
import socket
import sys
import tempfile
import time


# Support direct execution and importlib-based integration tests.
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from transfer_connectors import Protocol, LineConnector, CONNECTORS, download_begin, download_read, download_done, download_abort


def download(host,port,user,password,ident,destination,connector_factory=LineConnector,progress=None):
    if not 1<=ident<=0x7ffffffe:raise ValueError('Invalid file ID')
    destination=pathlib.Path(destination)
    if destination.exists() or destination.is_symlink():raise ValueError('Destination already exists')
    c=connector_factory(host,port,user,password); temporary=None
    try:
        size,expected=download_begin(c,ident); digest=hashlib.sha256();offset=0
        with tempfile.NamedTemporaryFile(prefix='.liner-download-',dir=destination.parent,delete=False) as f:
            temporary=pathlib.Path(f.name)
            while offset<size:
                data=download_read(c,ident,offset);count=len(data)
                if not count or offset+count>size:raise ValueError('Invalid chunk size')
                f.write(data);digest.update(data);offset+=count
                if progress:progress(offset,size)
            if digest.hexdigest()!=expected:raise ValueError('SHA-256 mismatch')
            if c.info(ident)!=(size,expected):raise ValueError('File metadata changed')
            f.flush();os.fsync(f.fileno())
        # Same-directory hard link publishes without overwriting even if another
        # process creates destination between the initial check and completion.
        os.link(temporary,destination)
        download_done(c,ident)
        return size,expected
    finally:
        download_abort(c,ident)
        c.close()
        if temporary is not None:temporary.unlink(missing_ok=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',default='127.0.0.1');p.add_argument('--port',type=int,default=2222)
    p.add_argument('--user',required=True);p.add_argument('--password-stdin',action='store_true',help='Read password from stdin, never from command-line arguments')
    p.add_argument('--connector',choices=CONNECTORS,default='line-v1')
    p.add_argument('id',type=int);p.add_argument('output')
    args=p.parse_args();password=sys.stdin.readline().rstrip('\r\n') if args.password_stdin else getpass.getpass('BBS password: ')
    try:
        size,digest=download(args.host,args.port,args.user,password,args.id,args.output,connector_factory=CONNECTORS[args.connector])
        print(f'Saved {size} bytes; SHA-256 {digest}')
    except (OSError,ValueError) as error:
        print(f'Download failed: {error}',file=sys.stderr);return 1
    return 0


if __name__=='__main__':
    raise SystemExit(main())
