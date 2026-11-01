#!/usr/bin/env python3
"""Build the pinned external ZMODEM engine into a NEW private directory (no root)."""
import argparse
import hashlib
import json
import difflib
import os
import pathlib
import subprocess
import tarfile
import urllib.request

URL = 'https://www.ohse.de/uwe/releases/lrzsz-0.13.1.tar.gz'
SHA256 = '112f91059fbd118e016d800c1a5c5a1702e965d8b9bfbda6015e44e20bfb93c9'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', help='New directory for source, build log and local installation')
    parser.add_argument('--archive', help='Optional pre-downloaded official archive; hash is still checked')
    args = parser.parse_args()
    if os.name != 'posix': parser.error('Build on FreeBSD or another POSIX host')
    root = pathlib.Path(args.directory).absolute()
    root.mkdir(parents=True, mode=0o700)  # No existing directory is overwritten.
    archive = root/'lrzsz-0.13.1.tar.gz'
    if args.archive:
        with open(args.archive,'rb') as f: data=f.read(2*1024*1024+1)
    else:
        with urllib.request.urlopen(URL,timeout=30) as response: data=response.read(2*1024*1024+1)
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError('Upstream archive SHA-256 mismatch')
    archive.write_bytes(data)
    with tarfile.open(archive) as bundle:
        # Explicit regular-file extraction: never create archive symlinks/devices.
        for item in bundle.getmembers():
            path = pathlib.PurePosixPath(item.name)
            if path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0]!='lrzsz-0.13.1':
                raise ValueError('Unexpected archive path')
            destination = root.joinpath(*path.parts)
            if item.isdir(): destination.mkdir(parents=True,exist_ok=True)
            elif item.isfile():
                destination.parent.mkdir(parents=True,exist_ok=True)
                with bundle.extractfile(item) as source, destination.open('xb') as target:
                    while chunk:=source.read(65536):target.write(chunk)
                destination.chmod(0o755 if item.mode & 0o111 else 0o644)
            else: raise ValueError('Unexpected non-regular archive entry')
    # LP64: ~unsigned-long sets the upper 32 bits, but ZCRC is a 32-bit wire value.
    # Preserve the pristine downloaded archive and record the exact local patch.
    source=root/'lrzsz-0.13.1/src/lrz.c';original=source.read_text()
    start=original.index('do_crc_check(FILE *f,');end=original.index('\n}',start)
    region=original[start:end]
    if region.count('crc = ~crc;')!=1:raise ValueError('Unexpected CRC implementation')
    changed=original[:start]+region.replace('crc = ~crc;','crc = (~crc) & 0xffffffffUL;')+original[end:]
    source.write_text(changed)
    (root/'liner-notes-crc32.patch').write_text(''.join(difflib.unified_diff(original.splitlines(True),changed.splitlines(True),fromfile='a/src/lrz.c',tofile='b/src/lrz.c')))
    prefix=root/'local' 
    # Upstream adds _POSIX_C_SOURCE, hiding S_IFMT on FreeBSD. Undefine it in
    # CFLAGS so FreeBSD's default API visibility is used; no upstream code edit.
    commands = [['./configure','CC=cc','CFLAGS=-O2 -U_POSIX_C_SOURCE',f'--prefix={prefix}'],
                ['make','-j2'],['make','install']]
    with (root/'build.txt').open('w') as log:
        log.write(f'Source: {URL}\nSHA256: {SHA256}\n');log.flush()
        for command in commands:
            subprocess.run(command,cwd=root/'lrzsz-0.13.1',stdout=log,stderr=subprocess.STDOUT,check=True)
    receiver=prefix/'bin/lrz'
    (prefix/'bin/lrz.liner-notes-resume.json').write_text(json.dumps({'patch':'crc32-lp64-v1','sha256':hashlib.sha256(receiver.read_bytes()).hexdigest()})+'\n')
    for program in ('lrz','lsz'):subprocess.run([str(prefix/'bin'/program),'--version'],check=True)
    print(f'Installed to {prefix}; upstream license/source retained in {root}/lrzsz-0.13.1')


if __name__=='__main__':main()
