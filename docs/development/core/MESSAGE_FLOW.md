# Gather and SYSOPMAIL session flow boundary

LN-23/24 work based on main `acc102b7a085490afbd1fc2e8e0b5fffb456580b`
(PR #124). This groups the public Gather read/write flow and the guest-capable
SYSOPMAIL compose/delivery flow. Commands, permissions, persistence, wire markers,
editor rules, UI messages and the Web connector remain unchanged.

## Ownership and dependencies

`message_flow_core.c/.h` owns per-session `GatherFlow` (mode, outstanding request,
next cursor, input line count) and `SysopMailFlow` (outstanding request and earliest
next compose time). They contain values only, are owned by the Session event loop,
and have no UI, worker, database, socket, credentials or authentication dependency.

Session maps command strings and worker action markers to semantic events, applies
Core effects, renders existing Japanese/English text and submits BoardRequests.
It owns the draft body and clears the **whole draft**, not just the text, after
entering Gather compose, cancellation or accepted posting. Core borrows a terminated,
nonoverlapping body/text buffer during input; it retains no pointers. Appends check
capacity before mutating the body or line count. Existing accepted Gather lines
fit well below the session draft capacity; the bound check does not lower a limit.

The existing Activity Core validator still checks UTF-8, controls and 70-character
lines. Session supplies that result to the flow; the worker independently validates
the entire Gather body, membership, feature setting and write result. SYSOPMAIL
still passes the current actor and credential epoch through submit; the existing
Mail/Core worker selects an enabled administrator and enforces feature/recipient
checks. Remembered mode/pending values do not authorize access or prove delivery.

## Preserved state and response handling

- Gather mode 1 reads then writes; mode 2 reads only. Pending mode 3 is a post.
  These values remain visible in the existing connector status contract.
- During reading, Enter/+ requests the next page; Q or // skips remaining pages.
  Read-only ends without composing. Read-then-write clears the draft and starts
  input. Other input while reading is ignored.
- While writing, a blank line counts; Q and + are literal text. Ten accepted lines
  submit automatically; // submits earlier, or cancels if no lines were entered.
  /CANCEL resets the active mode/cursor/line count and clears the draft in Session.
- Queue rejection creates no outstanding response. Worker failures retain the
  existing Gather state/body or resume SYSOPMAIL editing with its body intact.
  This is not a new retry protocol or a reset-on-login redesign.
- Session checks active/closing state and session ID/slot before passing responses
  to Core. Only the existing awaited branch consumes a response. Core does not
  replace the completion identity gate or reinterpret worker success markers.
- SYSOPMAIL title validation preserves the 1..80-byte/no-control rule. The 30-second
  per-connection compose cooldown starts on successful delivery, never on rejected
  queuing or failed delivery. Duplicate completion without pending state does not
  advance it. No new account-wide rate limit is introduced.
- Editor cleanup, connector guards, normalization and prompt selection use the
  same state through the two value objects. No auth/revoke lifetime is changed.

## Validation

Local Windows/GCC strict C11 build and execution of `test_message_flow_core` pass:
read-only/write transitions, ignored/read navigation input, empty and ten-line
posting, capacity boundaries, rejection/retry, cancellation, title bounds,
completion/cooldown boundaries, duplicates and independent sessions. These tests
supply a validity result; actual Unicode validation remains in Activity tests.

Strict syntax checks cover Session and the extended adapter test. Adapter execution
is delegated to existing Hosted CI: both languages, stale session/slot refusal,
body preservation on failure, read-only end, writing Q, cancellation and completion
cleanup. `tests/test_activity_core.py` additionally checks read-only command return
and empty-post cancellation, with both journal and SQLite storage; existing tests
already cover Unicode bounds, retention, restart, guests and disabled Gather.
`tests/test_stage57.py` covers guest SYSOPMAIL delivery and cooldown.

The new standalone target joins `make check`; existing adapter/socket suites run
in Hosted CI and, after merge, trusted-main native CI. Full runtime, packaging and
OS CI are not claimed as locally executed. Review CI results for the PR SHA before
merging. No production, release, workflow or native infrastructure change.
