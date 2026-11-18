# Door catalog and launch boundary (LN-23/24)

Base: PR #109 merge `fe34f4439e8e824a3a2cbc9dbd6c0491ab5af15e`.
This refactor groups configuration, discovery and launch selection. It does not
implement SSH, an authentication Broker, UID isolation or MMM.

## Responsibilities and ownership

- `door_core.h/c`: bounded owned catalog, six/seven-field grammar, duplicate and
  numeric validation, platform validation callback, level-filtered copy-owned
  `DoorListing`, and selection of an immutable `DoorSpec` for launch.
- `door_config.h/c`: file/stream loading, CRLF and final-line handling, read-error
  detection. A failed load clears the entire catalog. The stream function borrows
  its FILE; the path function owns and closes its FILE. NULL path disables games.
- `door_protocol.h/c`: legacy `GAMES / GAME / END GAMES` text formatting only.
  Insufficient capacity returns failure and an empty buffer, never a partial
  successful frame. Normal runtime responses are byte-compatible. At most 32
  summaries fit comfortably in the existing BoardResult buffer.
- `doors.c`: owns the startup catalog and verifies filesystem type/executability.
  Retains PTY, environment, DORINFO1.DEF, slot locking, process groups, deadlines,
  resize, nonblocking IO and asynchronous child reaping. Windows stubs stay disabled.
- `features64.inc`, `board.c`, `runtime.c`, `session.c`: existing authenticated
  worker request, epoch/revocation checks, session routing, menu and launch lifecycle.

The core has no member store, session, UI, socket, SQL, fork or PTY dependency.
Catalog loading completes before consumers start; live/concurrent reconfiguration
is not supported. A selected spec is borrowed until reload; list snapshots own
all their strings. Listing contains only ID, seconds and title, never executable
or working-directory paths. Callers may not mutate a catalog except through its
startup loading operations. No new persisted data or package settings are needed.

## Trust boundary

The worker checks broken member storage, suspension and current session epoch
before `advanced_execute`; its existing login check precedes action J. The level
comes from `members_level`, not the command input. The event loop checks connection
slot/session identity, authenticated user, closing/binary state and an existing
Door before launch. Launch selection repeats configured minimum-level checks and
rejects a zero user and effective root. Slot acquisition and filesystem checks
remain in the platform adapter. List filtering is not a substitute for launch ACL.

`door_core_list` and `door_core_launch` are trusted in-process APIs, not remote
endpoints or independent authentication. Future transports must retain the worker
and session checks; supplying a user ID/level is not proof of identity. Games still
run as the BBS UID. This refactor does not solve same-UID impersonation or provide
Broker tickets. StarRoute/Clans launch and persistent game data are unchanged.

## Compatibility and verification

Preserved: ordering, IDs, limits, title bytes, six-field unlimited-slot entries,
`none:N` / `dorinfo1:N`, comments, CRLF, last line without newline, startup failure
on any invalid entry, and unavailable/busy responses. The loader retains the
existing 4096-byte line reader. The native environment and process lifetime code
are not replaced. Public manual and JA/EN HELP clarify existing login/level rules.

`make test-door-core` is part of existing `make check`, with no new workflow or
VM. It tests numeric/options/path validation, missing validation callback,
duplicate IDs, full catalog, title/line boundaries, fail-closed reload, root and
guest rejection, unknown/restricted IDs, snapshot ownership, exact legacy output,
and every insufficient output size with guard bytes.

Existing `test_stage65_doors.py` now checks exact filtered RAW frames, guest list
rejection, missing-ID rejection and no path disclosure. `test_doors_config.py`
checks a valid row followed by a bad row, duplicate IDs, directory/executable
confusion, absent working directory, title controls and oversized lines. Existing
real Telnet/PTY tests cover menu return, resize, drops, slots across runtimes,
timeouts and abnormal/disconnected cleanup. They remain in `test-stage65`/`check`.

Local validation uses strict C11 GCC with warnings as errors for the pure modules
and Windows disabled-platform syntax; it does not establish native PTY behavior.
Actual FreeBSD/Linux/Solaris behavior must be reported against the exact CI SHA.
No production access or deployment is part of this change.
