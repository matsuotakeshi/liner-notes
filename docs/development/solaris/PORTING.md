# Solaris port verification record

This is development evidence, not a supported-installation claim. The canonical
user-facing status is in `docs/manual/install-solaris.md`.

## Initial trusted-main run

- Source: `d3b3af05e9ae6448d18f55cd24f49457913f74ca`.
- Actions: https://github.com/matsuotakeshi/liner-notes/actions/runs/42534580470
- Artifact: `native-ci-solaris-results`, ID `13639391335`.
- Result: FAILED during C compilation. No core runtime tests ran.
- Findings: GCC 15 strict misleading-indentation diagnostics; Solaris does not
  provide BSD `flock`/`LOCK_EX`/`LOCK_NB` in its C library headers.
- The fixes preserve `-Wall -Wextra -Werror`, split the diagnosed statements
  without altering C tokens, and add a Solaris-specific POSIX lock holder.
  FreeBSD/Linux retain their existing `flock` backend.

## Supplemental read-only inventory

Observed via the existing CI SSH account, keys and strict host-key checking;
no package installation, service operation or OS configuration change occurred.
Manual observations supplement Actions and do not replace native verification.

- Oracle Solaris 11.4 CBE x86-64, release `11.4.90.212.0`.
- GCC 15.2.0, GNU make, Python 3.11 and 3.13.9, OpenSSL 3,
  SQLite 3.51; cryptography available, aiohttp absent from system Python.
- `pkg-config` returns a 32-bit OpenSSL library directory; its existing `amd64`
  child is selected for the 64-bit build and runtime search path.
- `openpty` / `login_tty` declarations are provided by `termios.h`; these are
  libc interfaces, so the Solaris build must not request `-lutil`.
- `/usr/include/sys/fcntl.h` defines `F_CLOSEFROM` as 54. The installed
  `closefrom(3C)` manual (13 September 2024) explicitly identifies `closefrom`
  as Async-Signal-Safe, since Solaris 11.3.31. It distinguishes `fdwalk` as
  unsafe. The post-fork lock holder can therefore use `closefrom` on this target.
- The CI account has no SMF management authorization. Staging and
  `svccfg validate` do not prove service import, startup, shutdown or recovery.

## Remaining verification

The next trusted-main native run must finish core BBS regression, database
restart/backup/restore and PTY Door integration. WebUI/link dependencies are installed into a private venv only;
transfer tooling is built into a private prefix if absent. Failures remain CI
failures. SMF lifecycle testing requires a separately authorized suitable test
context and is not marked successful on the strength of manifest validation.

## Trusted-main build and partial regression

- Source: `310976797a53c32a62810612e9a847cd920902cc`.
- Actions: https://github.com/matsuotakeshi/liner-notes/actions/runs/42539443413
- Artifact ID: `13640937372`, SHA-256:
  `70f06871fc4b3b97505095b4ebc085d2f52fb2c385b2370e4778312ad0debe4e`.
- Passed: full strict C build; C Door lock lifetime units; SMF staging tests and
  native `svccfg validate`; doctor 31 units plus blocks/structured DB read-only
  checks; chat/userlog/session/Telnet and vault/transfer workflows before recovery.
- FAILED in `test_zmodem_recovery`: Python's Solaris flock emulation cannot lock
  a read-only directory. Full core, PTY integration, WebUI, link and real ZMODEM
  have not passed. SMF lifecycle was not executed and is separately unverified.
- Solaris headers have no `F_OFD_SETLK`/BSD flock extension. Python transfer and
  link locks therefore use a fresh isolated helper interpreter holding a POSIX
  record lock, with a pipe lease and separate session. Existing FreeBSD/Linux
  flock behavior is retained. A persistent validated private namespace file
  replaces directory locking only on Solaris. Its inode is never replaced by
  the transfer manager; protocol names cannot target it.
- Hosted tests must prove same-process conflict, unrelated close, thread wait,
  fork/exec inheritance, owner process-group death, legacy record-lock conflict,
  EOF cleanup, immediate reacquisition and namespace metadata rejection. An
  explicitly inherited pipe can keep a child lease; ordinary subprocesses do
  not inherit it. No service account privileges are changed.

The same known spool `EBADF` was reproduced after the FreeBSD-only visibility
fix, source `3bf05d7a0066d9d186b45ec68d352ce7f7cb8401`, Actions run
https://github.com/matsuotakeshi/liner-notes/actions/runs/42541514663 .
Artifact `13641109487` was downloaded and verified against SHA-256
`60af9b7463e8283bd7bf4eae00e6df825d5ef7692f33f14104139084788f7ffa`.
This is superseded diagnostic evidence, not a successful regression run.

PR #32 hosted Native CI Source Checks (run `42541498904`, head
`6c56b96e521e50edd33140119b6f695455b2c70e`) passed all seven Python lease tests
in 2.592 seconds, including the forced Solaris record backend on Linux. Strict
GCC build and Door integration also passed. This supplements, but does not
replace, the next Solaris native execution after main integration.

## FreeBSD regression recovery

The first C lock-holder merge exposed a FreeBSD strict-feature-namespace build
regression. PR #31 retains the native BSD namespace and flock backend. Trusted
main `3bf05d7a0066d9d186b45ec68d352ce7f7cb8401` passed the three-OS native gate
(run `42541514649`) and FreeBSD package build (run `42541514646`, artifact
`13642007059`, SHA-256
`342120bae0a97105bd60d3c1718c3ca361e195b5d26b222d8b0e8663c391e9fb`).
These results establish the recovery before the Python lock follow-up; that
follow-up must also pass its own main native and package tests.

## Native setup validation after Python lock merge

Source `0cb25f365dbced6b9a00a446c2a6f8068d5124cf`, Actions run
https://github.com/matsuotakeshi/liner-notes/actions/runs/42542437147 failed
before source upload or tests, with `Unexpected remote directory` (0.51 s).
Artifact `13642271595` was downloaded and verified as SHA-256
`4fe8c67b0a3594712a9620a4491b131f8ba3485080517af3d8558050ddf6a6d6`.
Read-only inspection through the existing CI account found Solaris mktemp
names including `native-solaris-XX1U_6Wa` and `native-solaris-XXtK_vjd`.
The validator accepted only alphanumeric suffixes. The fix permits underscore
in the Solaris suffix only, preserving the fixed CI parent/prefix and refusal
of traversal, extra path components, shell characters and extra output.
The Python lock changes have still not executed on Solaris at this point.

## Runtime progress and independent stty fixture

Source `e9f38ab59a3661358d8f69fe1ec2deea11f9aec3`, Actions run
https://github.com/matsuotakeshi/liner-notes/actions/runs/42559332489 ran for
370.231 seconds. Artifact `13649315958` SHA-256 verified as
`13a83f4e772fb0a31ef585462e9efea2e72e29bc1231450d8b14a1b94fb7deee`.
C/Python lease lifetime (seven Python tests), spool retry/retention, maintenance
backup/restore/compact, 12,000 article/mail RSS regression, permissions,
hierarchy, 256 KiB bodies, drafts and unread navigation passed.
The Door test passed NAWS/PTY size, SIGWINCH, invalid/duplicate/refused updates,
multiple sessions, crash/reentry/escape/disconnect/timeout assertions before
failing the final independent stty app: Solaris /usr/bin/stty has no `size`
option. Read-only inventory confirmed installed /usr/gnu/bin/stty. The fixture
selects that implementation only on Solaris and retains the exact 35x104
assertion; no runtime behavior or assertion is bypassed. Core as a whole,
remaining Door integration, WebUI, link and real ZMODEM remain pending.


## Final-main Door progress and next diagnostic

Trusted main `fc86d253755c06d3eda7da2351f799f7e4c65634`, Actions run
https://github.com/matsuotakeshi/liner-notes/actions/runs/42562818879 ran for
397.523 seconds. Artifact `13650652029` was downloaded and verified as SHA-256
`414fd990f4cd1c26581c5f721d5835d00783e2dc34fee9cf4933483c1d4ac925`.
The full NAWS/PTY test, including the exact independent GNU stty 35x104
assertion, passed. Doctor's 31 tests and actual blocks/structured DB diagnostics
both passed without changing source data, metadata or file inventory.

The next failure is `tests/test_doors_generic.py`: after single-slot exclusion
across two runtimes succeeds, starting the two-slot game waits for `KEY> ` and
times out. The existing exception omitted the received response, so its cause
is not yet established. The fixture now reports the test identity, game number,
buffered reply and runtime process state while retaining the same timeout and
assertions. Hosted CI additionally builds the full runtime with the Solaris
record-lock backend, and the lease unit test covers concurrent distinct slot
files and acquisition after a busy slot. No runtime behavior is changed on the
basis of an unconfirmed cause. Core as a whole, generic Door completion,
WebUI, link, real ZMODEM and SMF lifecycle remain unverified.


## Isolated ENOMEM diagnosis and tiny exec holder

Main `6d958e0f85f0e690be240cbeadc7f86c5371112a`, run
https://github.com/matsuotakeshi/liner-notes/actions/runs/42566262082 failed for
bob/game 3 with only input echo, while both runtimes remained alive. Artifact
`13651932277` SHA-256 was verified as
`0514c90babfc05fb669b7028fc1e6f4b55b5a9d17a713e0cd2d2514cdb0dc93e`.
The new concurrent distinct-slot C unit tests passed on Solaris.

A supplemental reproduction used the exact existing CI source (six relevant
source/test hashes checked), unchanged runtime SHA-256
`a5c71472c2e03704dfa5afdfe385924cdc0354601024acb6b4d07acfafda51ba`,
and newly generated fixture data. The final reproduction acquired the same
`~/ci/.liner-notes-native.lock` as native CI, after the normal cancellation and
cleanup of run `42568148241`. Only its own runtime PID 11627 was traced, without
following other processes. Directory:
`/export/home/ci-test/ci/solaris-door-diagnostic-ncun5lgc`.
The trace records successful forks for the existing holder/game, busy-slot
attempt and bob's new holder, followed by **forkx Err#12 ENOMEM** for bob's game.
Before the test swap reported 4,980,352 KiB available. The BBS and each old
long-lived holder have approximately 1 GiB virtual size; the holder retains
that reservation despite having only approximately 1 MiB RSS.

An earlier supplemental trace used the wrong lock path (home rather than
home/ci) and could overlap a native run. It is excluded from cause verification.
Its known trace/fixture processes were terminated by verified PID and private
argument path, without touching the native job or OS. The corrected isolated
trace above reproduced ENOMEM independently. No unreviewed application or test
code was executed on the VM; the final fix must still pass hosted PR checks and
trusted-main native CI.

The record holder now execs `liner-notes-door-lock`, a tiny image not linked
with the BBS, before acknowledging successful lock acquisition. Pipe inheritance,
separate process group, conflict behavior and known-PID reaping remain unchanged.
The executable is found relative to the actual BBS executable, not PATH or the
working/data directory; owner/mode/executable checks reject unsafe candidates.
The source build places it beside runtime and installers place it in libexec.
FreeBSD/Linux native flock does not execute the helper. Hosted regression creates
a 1 GiB owner and verifies the holder's distinct executable and virtual size
below 128 MiB, plus installed layout and unsafe helper mode rejection.
Solaris Door integration now runs before the long core suite to expose failures
earlier while preserving both suites and their assertions.

## Tiny holder native acceptance and receipt lock follow-up

Main `992d16910b06523503dd0bca72b3fc75bb52c298`, native run
https://github.com/matsuotakeshi/liner-notes/actions/runs/42571000213,
job `129008139155`, artifact `13652919042` (verified ZIP SHA-256
`0f8a555083dd2359480e29b29c2313b7e8573d71f14bdc3e6497e7650cc682cc`)
passed the complete Door suite twice, including the formerly failing bob/game 3
case, concurrent slots across two BBS runtimes, DORINFO, timeout/crash/escape,
disconnect and descendant cleanup. The tiny exec holder resolves the diagnosed
ENOMEM without changing assertions, timeouts, VM swap or OS configuration.
Doctor's 32 tests and both actual database read-only probes passed.

The run ended after 511.131 seconds at `tests/test_stage71.py`:
`ports/test-resume.py:save_receipt` still used flock on a read-only directory,
which Solaris emulates with an incompatible record lock (EBADF). The receipt
publisher now reuses `posix_locks.namespace_lock`; native FreeBSD/Linux keep
directory flock. Solaris uses the same fixed private empty `.namespace-lock`
as other namespace users, rejects that reserved receipt name, and never removes
the lock during publication. Existing receipt reuse, backup, atomic replacement
and symlink refusal remain. The module import works both from the source ports
directory and as installed `scripts/check-resume.py`, independently of cwd.

Remaining direct flock callsites were inspected: `src/door_lock.c` is the
FreeBSD/Linux branch; `scripts/serial_connector.py` rejects Solaris before device
opening; `scripts/deploy_host.py` only targets the declared FreeBSD/Linux hosts;
`ci/selinux_root.py` and `ci/freebsd_upgrade_root.py` are OS-specific helpers.
`ci/native_remote.py` holds one ordinary writable lock file for the worker's
lifetime, does not reopen/close its inode, and has passed the actual Solaris host
lease tests. Its process-owned semantics do not require a directory lease.
`scripts/posix_locks.py` selects its dedicated holder on Solaris. The direct
flock in `tests/test_freebsd_package_lease.py` is a FreeBSD package fixture.

Capabilities for this run are build/lock_lifetime/SMF-manifest/Door passed;
core incomplete; WebUI/link/ZMODEM not tested; SMF lifecycle unverified (no SMF
management authorization). This remains a failed native run, not full Solaris
acceptance. Receipt regression is added to hosted checks and runs early in the
next trusted-main Solaris CI as well as the full core suite.
