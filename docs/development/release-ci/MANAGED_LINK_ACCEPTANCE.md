# Managed shared-board link service acceptance

The operator supplied a 30-file SHA-256 inventory and sanitized production
feedback for the 0.75 three-site ring on 2026-11-28. All inventory entries were
checked before review. The reported result was three articles and one reply
at each site with no duplicates. That evidence establishes the behavior of
the previous setup only. The bundle and its deployment-specific identifiers
are kept outside the repository.

The FreeBSD service uses the supervisor PID, daemon process name, detached
standard descriptors, and the existing package's launcher. Its package treats
the rc.d file as an executable sample so an existing hand-installed service is
preserved. The local account uses the existing Ports private UID/GID allocation
scheme. Linux keeps the account and CLI already supplied by the RPM and DEB.
The services start independently of the BBS, with ordering when both are
started together. Incoming signed events remain in the link DB if the local
BBS is unavailable. No protocol, schema, or CD command changed.

## Verification

- Hosted Shared board visibility has the real BBS/mTLS suite, including a new
  BBS-only outage test. It checks that a signed event enters the pending queue
  during the outage, applies after BBS restart, and leaves the same connector
  PID and signing identity in use.
- The same workflow exercises the packaged Linux unit with isolated paths and
  an unprivileged fixture account. It checks unit verification, no initial
  start/enable, restart, crash recovery, status socket and cleanup.
- Debian and Rocky package CI preserve the administrator unit and Link state
  on a revision upgrade and removal. The Debian test also checks config
  permissions. Rocky retains state ownership by a pre-existing account.
- The existing FreeBSD isolated upgrade jail now exercises the packaged
  rc.d definition as installed by pkg, its account and default-off state,
  repeat start, status, restart, stop and identity preservation. The previous
  real-daemon link and rollback checks are retained.
- The existing Rocky Enforcing test runs a confined connector exercising
  signed import/export, key rotation and an actual BBS connection. The hosted
  unit test complements this coverage. It does not establish packaged-unit
  service behavior on an enforcing Rocky host.
- A complete host reboot, native Rocky/Debian packaged-service lifecycle,
  a fresh remote installation using only the manual, and an SSH-driven rc.d
  start on FreeBSD require separate acceptance. Record result URLs and the
  tested SHA before reporting any of these as verified.


Local Windows checks passed Python and shell syntax, Link storage/topology
(2 tests), TLS/identity rotation (3 tests), deployment components (6 tests),
and the canonical manual (42 pages and 2872 local links). The POSIX-only
checkpoint retest cannot run on this Python because it lacks O_NOFOLLOW.
