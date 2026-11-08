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

The next trusted-main native run must prove the C build, core BBS and management
commands, database restart/backup/restore, PTY Door behavior and inherited-lock
lifetime. WebUI/link dependencies are installed into a private venv only;
transfer tooling is built into a private prefix if absent. Failures remain CI
failures. SMF lifecycle testing requires a separately authorized suitable test
context and is not marked successful on the strength of manifest validation.
