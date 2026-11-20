#!/usr/bin/env python3
"""Build the pinned external ZMODEM engine into a NEW private directory (no root)."""
import argparse
import hashlib
import json
import difflib
import os
import sys
import pathlib
import subprocess
import tarfile
import urllib.request
import urllib.error
import urllib.parse
import http.client
import ssl
import tempfile
import time

URL = 'https://www.ohse.de/uwe/releases/lrzsz-0.13.1.tar.gz'
SHA256 = '112f91059fbd118e016d800c1a5c5a1702e965d8b9bfbda6015e44e20bfb93c9'

MAX_ARCHIVE = 2 * 1024 * 1024
ATTEMPTS = 3
RETRY_DELAYS = (2, 5)


class HTTPSRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if urllib.parse.urlsplit(newurl).scheme != 'https':
            raise ValueError('Archive redirect must remain HTTPS')
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def verify_archive(data):
    if len(data) > MAX_ARCHIVE:
        raise ValueError('Upstream archive exceeds size limit')
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError('Upstream archive SHA-256 mismatch')
    return data


def read_archive(path):
    with open(path, 'rb') as stream:
        return verify_archive(stream.read(MAX_ARCHIVE + 1))


def download_archive():
    opener = urllib.request.build_opener(HTTPSRedirect())
    for attempt in range(ATTEMPTS):
        try:
            with opener.open(URL, timeout=30) as response:
                data = response.read(MAX_ARCHIVE + 1)
            return verify_archive(data)
        except (urllib.error.URLError, TimeoutError, ConnectionError, http.client.IncompleteRead) as error:
            # Do not retry integrity failures, invalid certificates or permanent
            # HTTP errors. Every successful retrieval still uses the fixed hash.
            reason = getattr(error, 'reason', error)
            if isinstance(reason, ssl.SSLCertVerificationError):
                raise
            if isinstance(error, urllib.error.HTTPError):
                error.close()
                if error.code not in (408, 429, 500, 502, 503, 504):
                    raise
            if attempt + 1 == ATTEMPTS:
                raise
            delay = RETRY_DELAYS[attempt]
            print(f'Archive retrieval failed ({type(error).__name__}); retry {attempt + 2}/{ATTEMPTS} in {delay}s',
                  file=sys.stderr, flush=True)
            time.sleep(delay)


def ensure_archive(path):
    """Recheck cache hits; publish verified bytes without replacing any file."""
    path = pathlib.Path(path)
    if path.exists() or path.is_symlink():
        read_archive(path)  # Bad cache is an error, never silently overwritten.
        print(f'Archive SHA-256 verified: {SHA256} (cached)', flush=True)
        return
    data = download_archive()
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix='.lrzsz-download-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data); stream.flush(); os.fsync(stream.fileno())
        os.chmod(temporary, 0o644)  # Readable by the non-root container test user.
        try:
            os.link(temporary, path)  # Atomic publication, refuses overwrite.
        except FileExistsError:
            read_archive(path)
    finally:
        os.unlink(temporary)
    print(f'Archive SHA-256 verified: {SHA256} (downloaded)', flush=True)


def build_commands(prefix, system, compiler=None, make=None):
    # Preserve FreeBSD/Linux defaults. Solaris commonly installs gcc/gmake but
    # not cc; all outputs still go under the caller's new private prefix.
    solaris = system.startswith('sunos')
    compiler = compiler or ('gcc' if solaris else 'cc')
    make = make or ('gmake' if solaris else 'make')
    cflags = '-O2'
    if system.startswith('freebsd'): cflags += ' -U_POSIX_C_SOURCE'
    elif system.startswith('linux'): cflags += ' -D_DEFAULT_SOURCE'
    elif solaris: cflags += ' -m64 -D__EXTENSIONS__=1'
    return [['./configure', 'CC=' + compiler, 'CFLAGS=' + cflags, f'--prefix={prefix}'],
            [make, '-j2'], [make, 'install']]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', help='New directory for source, build log and local installation')
    parser.add_argument('--download-archive', help='Fetch or verify a pinned source archive without building')
    parser.add_argument('--archive', help='Optional pre-downloaded official archive; hash is still checked')
    parser.add_argument('--cc', help='C compiler for this isolated build')
    parser.add_argument('--make', help='Make implementation for this isolated build')
    args = parser.parse_args()
    if args.download_archive:
        if args.directory or args.archive or args.cc or args.make:
            parser.error('--download-archive cannot be combined with build arguments')
        ensure_archive(args.download_archive)
        return
    if not args.directory: parser.error('directory is required for a build')
    if os.name != 'posix': parser.error('Build on FreeBSD or another POSIX host')
    root = pathlib.Path(args.directory).absolute()
    root.mkdir(parents=True, mode=0o700)  # No existing directory is overwritten.
    archive = root/'lrzsz-0.13.1.tar.gz'
    data = read_archive(args.archive) if args.archive else download_archive()
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
                # Keep generated configure/Makefile.in newer than their inputs.
                # Resetting every mtime to extraction time spuriously requires
                # the release author's Autoconf/Automake on a clean Linux host.
                os.utime(destination,(item.mtime,item.mtime),follow_symlinks=False)
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
    commands = build_commands(prefix, sys.platform, args.cc, args.make)
    with (root/'build.txt').open('w') as log:
        log.write(f'Source: {URL}\nSHA256: {SHA256}\n');log.flush()
        for command in commands:
            subprocess.run(command,cwd=root/'lrzsz-0.13.1',stdout=log,stderr=subprocess.STDOUT,check=True)
    receiver=prefix/'bin/lrz'
    (prefix/'bin/lrz.liner-notes-resume.json').write_text(json.dumps({'patch':'crc32-lp64-v1','sha256':hashlib.sha256(receiver.read_bytes()).hexdigest()})+'\n')
    for program in ('lrz','lsz'):subprocess.run([str(prefix/'bin'/program),'--version'],check=True)
    print(f'Installed to {prefix}; upstream license/source retained in {root}/lrzsz-0.13.1')


if __name__=='__main__':main()
