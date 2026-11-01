# lrzsz 0.13.1 FreeBSD port

`comms/lrzsz/` is a complete port. `lrzsz-0.13.1-freebsd.patch` updates the
FreeBSD ports main snapshot fetched on 2026-10-30. The snapshot file hashes are
in `upstream-snapshot.json`; refresh the diff if the upstream port has changed.
No PR has been submitted and no system installation was replaced.

## Source and patch

- [Upstream release](https://www.ohse.de/uwe/releases/lrzsz-0.13.1.tar.gz)
- [Upstream project](https://ohse.de/uwe/software/lrzsz.html)
- [FreeBSD port](https://github.com/freebsd/freebsd-ports/tree/main/comms/lrzsz)

Release SHA-256: `112f91059fbd118e016d800c1a5c5a1702e965d8b9bfbda6015e44e20bfb93c9`.
Use the authoritative value in `comms/lrzsz/distinfo` if copying a checksum.

The local patch masks the complemented CRC to 32 bits in `do_crc_check`.
On LP64, an unmasked unsigned long complement retains high bits; comparison
against the peer's 32-bit ZCRC then rejects an otherwise matching prefix.
This makes a resumed receive restart at byte zero. The fix leaves the protocol
unchanged and allows the existing matching prefix to be used.

The port uses GNU make and clears `_POSIX_C_SOURCE` from the compilation command
because the upstream configure setting hides `S_IFMT` on FreeBSD. NLS is disabled:
the 0.13.1 release ships an empty `po/LINGUAS`, so the old German catalog entry
and NLS option no longer describe this release's installed files.

Old patches for metadata initialization, sender file size initialization,
IEXTEN, zero-length packets, and executable DESTDIR handling are present in the
new upstream source. The old gettext installation patch is unnecessary with
NLS disabled. Upstream also handles a missing XMODEM filename without the old
NULL dereference (using `stdin` as the fallback name); this is not the old
port's command-line usage rejection. X/YMODEM interoperability was not tested.

## Build in a separate ports checkout

Apply the diff at the root of a writable FreeBSD ports checkout:

```sh
git apply --check /path/to/lrzsz-0.13.1-freebsd.patch
git apply /path/to/lrzsz-0.13.1-freebsd.patch
cd comms/lrzsz
make stage check-plist stage-qa package
```

Alternatively copy the supplied `comms/lrzsz` directory into a separate checkout.
Keep the files in LF format. `make install` is an administrator's separate step;
the checks above only stage and package the software. Existing ports build
dependencies, including GNU make, must be available.

On the test host the port was built with a separate WRKDIRPREFIX under the
stage57 directory. FreeBSD 15.1/amd64: stage, check-plist, stage-qa, and package
all passed. This is a host build, not a poudriere matrix across supported releases.

## Verify actual resume

```sh
python3 test-resume.py /path/to/lrz /path/to/lsz --receipt "$PWD/lrz-resume.json"
```

The test creates a 1MiB payload and retains its first 768KiB. It checks both
completed SHA-256 and sender wire-byte count, rejecting a full retransmission.
With the staged port binaries the sender used 329,404 bytes; the completed hash
was `fbbab289f7f94b25736c58be46a994c441fd02552cc6022352e3d86d2fab7c83`.

The optional receipt is tied to the exact receiver executable's SHA-256.
Liner notes accepts it through `configure-zmodem.py --resume-receipt PATH`;
it is not installed as part of the port and is not a claim about another binary.
Replacing the executable requires a new test and receipt.

The staged binaries also passed 256MiB single-file and 16-file/256MiB batch
wire transfers with complete hashes, plus Liner notes menu and mail workflows.
Detailed test logs are included in the main stage57 evidence bundle.

## Suggested PR description

comms/lrzsz: update to 0.13.1 and fix LP64 ZCRC resume comparison

Update the release, checksum and build configuration. Replace obsolete 0.12.20
patches with the LP64 CRC comparison fix: mask the complemented CRC to 32 bits
so a matching partial file resumes instead of retransmitting from zero.

Use GNU make, expose the FreeBSD file type macros, and disable the empty NLS
catalog set. Preserve the current maintainer and GPLv2+ license declaration.

Validation: FreeBSD 15.1 amd64 stage/check-plist/stage-qa/package; actual paired
receiver/sender resume of a 1MiB file from a 768KiB prefix, with 329,404 sender
wire bytes and matching completed SHA-256; 256MiB and 16-file batch transfers.
Other FreeBSD releases, poudriere and X/YMODEM interoperability remain untested.
