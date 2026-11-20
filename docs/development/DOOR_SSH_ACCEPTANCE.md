# Door Broker / built-in SSH acceptance expansion

Date: 2026-11-20. Base main: `145fbaec40e276f38da0861f4e3bc96a7d447a64`.
This is candidate verification work, not a release or production activation.

## Previous DB gate

PR #151 head `967684873ea2c7ee4e979a6936b66333d06b409a` passed all 19
reported checks and was squash-merged into the base above. The previous
FreeBSD pre-upgrade restore failure was a schema-aware validation defect, not
a Door/SSH failure. Exact-main isolated upgrade acceptance run 44683106388 and Release Set run
44683106213 subsequently passed for that SHA. PR #152 was then merged as
`ed010ee3c6a7ecbb1b64e694b7839264832fb418`; later evidence must match that SHA.

## Added checks and execution routes

| Scope | Added assertions | Existing execution route |
| --- | --- | --- |
| Broker consumer | Wrong kernel peer UID gets no credential bytes; outage never uses cached authority; every identity component remains bound | `tests/test_door_broker.py`, `make check` |
| Broker protocol | 16 concurrent consumers of one ticket produce exactly one lease; issuer disconnect revokes it | Same portable suite |
| Isolated Broker | SIGKILL with live Door/detached child, lease loss, cleanup, outage admission denial, restart rejects cached leases, fresh login preserves game records | Existing Hosted Linux privileged fixture; trusted-main FreeBSD jail and Rocky Enforcing fixture |
| Encrypted SSH | SCP/X11/invalid PTY refusals, authenticated startup deadlines, active member suspension with Door, shared four-slot cap and aborted-handshake recovery | Existing Linux compatibility jobs, `tests/test_ssh_transport.py` |

The Broker crash fixture removes only its own orphan socket after checking the
dead generation's inode, owner and socket type, and verifying Door cleanup.
It does not clear BUSY records or install automatic recovery. Existing service
adapters are reused for Linux systemd and FreeBSD rc. Committed synthetic game
data must remain unchanged through the outage; this is not new AREA-PC88 evidence.

Local Windows: the portable Broker suite passed 23 tests; SSH source/screen
checks passed 6 with 3 existing POSIX PTY checks skipped. Syntax/diff checks
passed. The Windows restricted process environment stalled the asyncio suite;
the same tests passed with a bounded, approved local execution. This is not a
native Broker isolation or encrypted SSH success. New OS integration cases are
pending the PR/main CI sequence; full regressions were not duplicated locally.

## Solaris failure investigation

- Failed scheduled run [44435853049](https://github.com/matsuotakeshi/liner-notes/actions/runs/44435853049),
  source `663e7ec15bd7edb90e1c0846d79ffc0821b24166`, artifact `14489067341`:
  GCC rejected the `snprintf` append in `application_form_input` with
  `-Werror=format-truncation`. This occurred during compilation, before runtime tests.
- Existing fix `f39354fbe679df3e5cf0a1c28e8dc67cf08b8f93` checks complete
  record capacity and copies bounded fields; it is already in main.
- Later scheduled run [44653001865](https://github.com/matsuotakeshi/liner-notes/actions/runs/44653001865)
  succeeded at `b8d980fba71c6aab3565f10b29ef341924b82b47`. No duplicate repair,
  old-failure rerun or extra daily Solaris run is needed for that warning.
  This success does not validate later SHAs, encrypted SSH or Broker isolation.

## Remaining boundaries

- New Linux encrypted / privileged cases must pass Hosted CI; the expanded
  service fixture needs exact-main FreeBSD jail and Rocky Enforcing results.
- FreeBSD/Solaris encrypted libssh runtime acceptance remains unverified;
  ordinary native builds without libssh do not cover it. SSH SELinux host-key
  and port labeling/denial acceptance is separate from Broker policy acceptance.
- Solaris Broker isolated launch still has no contracts/privilege backend and
  remains fail-closed. Same-UID mocks or kernel-peer tests do not complete it.
- Independent security review and broader host resource exhaustion remain
  open. The small bounded load tests do not claim host-wide DoS resistance.
- Existing private AREA-PC88 results remain tied to their two source SHAs;
  no game source, production game data, host package or installed helper changes.

The stable manual's Door and connection chapters were reviewed. Public text
keeps stable 0.74.1 separate from these candidates; no HELP command changes.

## Next grouped candidate

[SSH platform/SELinux acceptance](SSH_PLATFORM_ACCEPTANCE.md) records the next
implementation, its dependency gates and Broker quota correction. Native SSH
and independent security review remain separate gates, not inferred from older results.

## Verified base after PR #152 merge

All 11 reported main workflows succeeded at
`ed010ee3c6a7ecbb1b64e694b7839264832fb418`, including
[FreeBSD isolated upgrade](https://github.com/matsuotakeshi/liner-notes/actions/runs/44685732715),
[Rocky SELinux](https://github.com/matsuotakeshi/liner-notes/actions/runs/44684400373),
[Multi-OS](https://github.com/matsuotakeshi/liner-notes/actions/runs/44684400350) and
[Release Set](https://github.com/matsuotakeshi/liner-notes/actions/runs/44685732682).
These attest the base's Broker outage/lifecycle cases, not the next candidate's
SSH labels or libssh-enabled native runs. Later VM dependency changes likewise
are not retroactively covered by those receipts.
