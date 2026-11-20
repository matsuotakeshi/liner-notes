# Native failures after PR #154

Source: main `75b85c307e540116e4e5666b9557732abc098458`.
PR #155 head `3c5091a0919490ff5cd8f1472810d1665d240dfe` passed all eleven
Hosted workflows. That does not override these failed main native results.

## Solaris core

[Run 44699155477](https://github.com/matsuotakeshi/liner-notes/actions/runs/44699155477),
Artifact `14613706188`: `test_stage70_scale.py` invoked hard-coded `cc` and
failed with FileNotFoundError. The host has `/usr/bin/gcc`, not cc. The fix moves
`generate-structured` into Makefile with the selected compiler, 64-bit CPP/C/link
flags and SQLite/OS libraries, then passes that executable to the unchanged
12,000 article/mail, 48 MiB RSS, read/search/SEEN/restart/integrity assertions.
No timeout or scale threshold is relaxed. The generator is removed by `clean`.

## Solaris SSH

[Run 44699152476](https://github.com/matsuotakeshi/liner-notes/actions/runs/44699152476),
Artifact `14612599402`: libssh refers to `__stack_chk_guard` and
`__stack_chk_fail`. Direct `ldd -r` on the CI account confirmed both unresolved
symbols. Select the actual compiler's 64-bit libssp with `-print-file-name`,
link `-lssp` and record its resolved directory in the executable runpath. Missing
runtime fails explicitly. Stack protection is not disabled.

With that command-line adjustment, an independent copy of the verified main
archive built successfully and `ldd -r runtime` resolved all symbols. The native
contract probe compiled and passed. This is a main-code diagnostic with adjusted
build arguments, not a successful final candidate CI run.

Encrypted connection testing then found a separate dependency issue: Solaris
vendor Paramiko 4.0.0 has neither Ed25519Key nor ssh-ed25519 in its preferred host
key types. The upstream Ed25519 acceptance must not be replaced by weaker tests.
Private upstream Paramiko 4.0.0 / PyNaCl 1.6.2 wheels were successfully prepared
from fixed official source hashes and installed only in the CI account venv. Candidate native CI installs an available prepared
manifest offline with hash verification into a fresh venv only, keeps the common
version requirements, and explicitly checks Ed25519 capability before connection
tests. Missing support remains a failure, never a skip.

Local evidence: `build/failures-75b85c3/solaris-direct-read.txt`,
`solaris-direct-test.log`, `solaris-paramiko-capability.txt` in the same directory.
Diagnostic main copy: `/export/home/ci-test/ci/solaris-diagnose-75b85c3-olu5lxvr`.
The original failed CI copies were preserved. No OS/service/account changes.

## Rocky Enforcing

[Run 44699010695](https://github.com/matsuotakeshi/liner-notes/actions/runs/44699010695),
Artifact `14613133126`: second supervisor SIGKILL/restart/re-entry timed out.
The BBS logged isolation receive errno 11; the later worker logged errno 32 at
handoff. Those messages show a lost handoff deadline, not its cause or an AVC
permission denial. The cause is **unresolved**.

Opt-in diagnostics now record only worker PID, a fixed launch phase and elapsed
milliseconds. Private artifact filtering allowlists those fields and drops
arbitrary messages, requests, credentials and paths. Normal operation stays
silent. Existing timeouts and SELinux permissions are unchanged. After candidate
review/main integration, rerun the existing Enforcing acceptance on that SHA and
use the phase evidence to select a real fix; do not claim this instrumentation
has fixed the native failure.

## FreeBSD test dependencies

The approved five packages were installed after acquiring the native host lock.
Both initial plans listed only five additions, but pkg's post-download resolution
also upgraded py311-wheel 0.46.3_1 to 0.48.0. Inventory comparison detected this;
further FreeBSD changes were stopped and the user explicitly chose to retain
0.48.0. No rollback, service restart or production operation was performed.

Read-only CI-account imports then verified Paramiko 3.5.1, cryptography 50.0.1,
bcrypt 5.0.0, PyNaCl 1.6.2 and Ed25519Key availability. This is dependency readiness,
not FreeBSD encrypted acceptance. Logs: `build/freebsd-paramiko-install-final.log`
and `build/freebsd-paramiko-verify.txt`. Future constrained package preparation
must also account for pkg's post-fetch re-resolution; a successful dry-run alone
is not a guarantee of an unchanged transaction.


## Reproducing the Solaris private SSH client preparation

The source list and SHA-256 values are retained in
[SSH_CLIENT_SOURCES.json](solaris/SSH_CLIENT_SOURCES.json). Download those exact
files using normal HTTPS verification and verify every digest before use. The
original pip-online build timed out after 360 seconds during build-dependency
installation; a separate offline venv was selected instead. Logs are
`build/solaris-ssh-client-wheels.log` and `build/solaris-ssh-client-offline.log`.

Use the existing non-root CI account, acquire its existing native host lock,
and prepare a private directory; do not install into system Python. The concrete
instance is `~/liner-notes-deps/ssh-client-4.0.0`. Its `offline` directory holds
the verified files and `offline-venv` is created with `--system-site-packages`.
The installed Solaris Python 3.13 supplies setuptools 80.9.0, cffi 2.0.0,
cryptography 46.0.3 and bcrypt 5.0.0. Install only the verified wheel/packaging
files into this venv using `pip install --no-index --no-deps`.

Build the verified PyNaCl source with this private Python using
`pip wheel --no-index --no-deps --no-build-isolation ... -w wheels` and environment
`CC=gcc`, `CFLAGS=-m64`, `CPPFLAGS=-D__EXTENSIONS__=1`, plus
`LDFLAGS=-m64 -lssp -Wl,-R,<resolved compiler libssp directory>`.
Use `gcc -m64 -print-file-name=libssp.so` and resolve the existing symlink for that
directory, rather than changing the system loader path. Build processes must have
a bounded deadline and their own process group for cleanup.

Copy the verified upstream Paramiko wheel alongside the resulting PyNaCl wheel.
Write `wheels/requirements.txt` containing the absolute path and SHA-256 of each
of those two wheels. Install that manifest into the private venv using
`pip install --no-index --no-deps --ignore-installed --require-hashes -r ...`.
Check that importing Paramiko exposes `Ed25519Key` and ssh-ed25519 in the transport
host-key list. A prepared client still does not attest encrypted BBS acceptance.
The candidate native harness performs the same hash-checked offline installation
into each fresh test venv, then verifies common requirement bounds and capability.
Do not reuse partial wheel output as successful preparation.

## Direct Solaris SSH retest and remaining gates

The prepared upstream client passed the existing main SSH integration suite in
`/export/home/ci-test/ci/solaris-diagnose-75b85c3-olu5lxvr`, with the compiler runtime
link adjustment described above. Command (under the existing native host lock):

```sh
cd /export/home/ci-test/ci/solaris-diagnose-75b85c3-olu5lxvr
~/liner-notes-deps/ssh-client-4.0.0/offline-venv/bin/python -B tests/test_ssh_transport.py ./runtime
```

Exit 0, `SSH integration passed`. Coverage included password/key account binding,
UTF-8, Telnet coexistence, OpenSSH PTY/PING, Door return/crash/timeout/resize and
connection cleanup, forbidden exec/subsystem/forwarding/channel requests, startup
and handshake deadlines, attempt limits and shared connection-slot reuse. The
negative connection tests deliberately emit administrative-prohibition and banner
reset diagnostics; these were followed by their successful rejection assertions.
Log: `build/failures-75b85c3/solaris-ssh-retest.log`.

Generated native PyNaCl wheel SHA-256:
`62c017341d77474fb47bd571a07227d32c86f887f074cf58f9b50763d6d421b6`.
Private preparation ended with `PREPARATION_COMPLETE`; no system Python or OS
service was changed. This validates a main-code diagnostic, not PR #155's final
commit or Solaris authenticated Broker isolation. Those remain separate gates.

Local candidate checks: native SSH harness 10 passed; worker diagnostics 14 passed;
SELinux service harness 50 passed; Solaris port checks 8 passed / 1 native compiler
skip. Canonical manual: 42 pages, 2,822 local links, Markdown/HTML/offline parity,
no errors (`build/solar-failure-fixes/check.json`). The updated stage70 generator
requires CI execution; no large-scale assertion has been weakened. After Hosted
checks and review, exact-main native runs must verify the candidate changes.
Rocky Enforcing's handoff-latency cause is still unresolved, so its next run is a
diagnostic run and must not be reported as fixed in advance.
