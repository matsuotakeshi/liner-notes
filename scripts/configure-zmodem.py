#!/usr/bin/env python3
"""Create a new runtime helper launcher with verified engine paths; never overwrite."""
import argparse,os,pathlib,shlex,sys
from zmodem_connector import ZmodemProcess

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--receiver',required=True);p.add_argument('--sender',required=True)
    p.add_argument('--spool-root',required=True);p.add_argument('--output',required=True)
    p.add_argument('--resume-receipt',help='Receipt from ports/test-resume.py for a packaged receiver')
    p.add_argument('--timeout',type=int,default=120);a=p.parse_args()
    engine=ZmodemProcess(a.receiver,a.sender,a.timeout)
    receiver=engine.executable(a.receiver);sender=engine.executable(a.sender)
    spool=pathlib.Path(a.spool_root).absolute();output=pathlib.Path(a.output).absolute()
    if not spool.exists():spool.mkdir(mode=0o700,parents=True)
    if not spool.is_dir() or spool.is_symlink() or spool.stat().st_uid!=os.getuid() or spool.stat().st_mode & 0o077:
        p.error('Use a private 0700 spool directory owned by the runtime account')
    command=[str(pathlib.Path(sys.executable).resolve()),str(pathlib.Path(__file__).with_name('zmodem_host.py').resolve()),'--receiver',receiver,'--sender',sender,'--spool-root',str(spool),'--timeout',str(a.timeout)]
    receipt=None
    if a.resume_receipt:
        receipt=str(pathlib.Path(a.resume_receipt).resolve());os.environ['LINER_NOTES_RESUME_RECEIPT']=receipt;engine._verify_resume_receiver()
    fd=os.open(output,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o700)
    with os.fdopen(fd,'w') as f:f.write('#!/bin/sh\n'+('export LINER_NOTES_RESUME_RECEIPT='+shlex.quote(receipt)+'\n' if receipt else '')+'exec '+shlex.join(command)+' -- "$@"\n')
    print('Created:',output)
    print('Runtime option: --zmodem-helper',shlex.quote(str(output)))
if __name__=='__main__':main()
