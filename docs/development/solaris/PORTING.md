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
