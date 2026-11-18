# Connection lifecycle Core (LN-23/24)

## Responsibility and trust boundary

`connection_core.h/.c` owns explicit policy, per-connection clock state and shutdown
state. It has no Session, UI, socket, filesystem, database or global mutable state.
The event-loop adapter supplies a snapshot of the current authenticated member,
level, administrator flag and execution mode. These fields are trusted internal
inputs, never values accepted directly from a client. Activity Core still checks
administrator authority and records control requests before runtime applies them.
Neither the lifecycle API nor its protocol formatter is a new public endpoint.

`runtime.c` owns sockets, session lifetimes, process/transfer cleanup, saved user
settings, USERLOG updates and translated notices. `connection_protocol.c` formats
owned ConnectionSummary rows into the existing administrative CONNECTIONS response.
The formatter fails with an empty output on overflow; no partial list is accepted.
Snapshots contain login, peer, mode and counters, not passwords or message bodies.

## State ownership and order

Each live Connection owns a zero-initialized ConnectionClock. Policy is runtime
configuration shared read-only after startup. One ConnectionShutdown is owned by
the runtime event loop. No object is shared with a worker or another runtime.
Pointers are borrowed for the duration of a call only. Row strings and shutdown
reason are copied into owned fixed-size fields. Callers provide non-null objects,
terminated strings, validated configuration and a monotonic millisecond clock.
Zero deadlines mean no pending action, as in the previous runtime implementation.

The event-loop order remains:

1. Update connection history and evaluate level duration (since connection start).
2. Apply hard drop, or render warning/grace notices using the session language.
3. Finish an existing idle close after its output-drain deadline.
4. Count binary, pending and ZMODEM work as activity, then advance the session.
5. Refresh the member/mode snapshot after advance, evaluate idle time, and render
   warning or close output. A dropped slot is never evaluated again.

Time-limit warnings/grace flags remain connection-scoped across login/logout.
Administrators and closing sessions are exempt. Door, binary, ZMODEM and secret
input suppress duration notices, not the hard deadline. Idle notices require menu
mode and non-secret input; expiry still applies to RAW and secret input. Pending,
binary and ZMODEM work are exempt from idle expiry. Door input resets activity;
Door execution alone is not an idle exemption. Backward clock readings clamp to 0.

Idle expiry allows 1000 ms to drain the closing message. Shutdown starts a 500 ms
drain, rejects new accepts, closes active transfers as before and leaves the
actual teardown in runtime. A replacement schedule clears the old drain state;
cancellation clears the complete schedule. No authorization decision moved here.

## Verification

`make test-connection-core` (also included in `make check`) deterministically tests
warning/grace/drop boundaries, disabled limits, guest/invalid level fallback,
administrator exemptions, mode priority, notice suppression versus hard expiry,
input and busy activity, independent clocks, shutdown replacement/cancellation,
legacy report bytes, 64-bit counters and every insufficient output size.
`tests/test_stage57.py` checks the real administrative list columns, users, peer
and counters, authorization denial, disconnect and shutdown/cancel behavior.
Existing stage59/stage61 and system-control tests exercise idle/admin/time limits
and the Web connector. They remain in existing CI; no home VM runs unreviewed PRs.

Local direct C tests passed with GCC, strict warnings enabled. A Windows runtime
syntax check is blocked by pre-existing storage_lock struct flock declaration and
TZ misleading-indentation warnings. No Windows full-runtime success is claimed;
FreeBSD/Linux runtime and regression validation belongs to the existing CI.
This extraction adds no SSH transport, new configuration or persistence format.
