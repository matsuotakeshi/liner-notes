# Sequential reading and new-item traversal (LN-23/24)

Baseline: main `bf133f5d61960fc9798379b9a646c6b0f545dc4a` (PR #119).
This unreleased refactor groups article/mail movement, stop handling and
article/mail/file checkpoint traversal. It does not change commands, ACLs,
storage formats, timestamps or the worker's result selection.

## Boundaries and ownership

- `traversal_core.c/.h`: session-owned `ReaderState`, position calculation,
  pending-read cancellation, sequence completion and `CheckTraversal` lifecycle.
  `CheckCommit` is an owned snapshot of kind, scope key and traversal end time.
  There is no Session, BoardRequest, UI, database, socket or authorization call.
- `traversal_protocol.c/.h`: existing CHECK target grammar and trusted worker
  `END NEXT` marker decoding. Invalid targets keep the previous syntax response.
  This retains the legacy marker grammar; it is not a new untrusted API parser.
- Session: commands, prompts, pagination, translations, worker dispatch and
  completion identity/slot guards. Its retained scan request owns board, author,
  term, scope and direction. Continued requests refresh user epoch and grants;
  the existing Article/Mail/Vault Core checks ACLs on every worker operation.
- Runtime: `session_auto_read`, `session_scan_stop` and `session_scan_pause`
  replace direct scan flag access. Board refresh uses `session_board_refresh`
  to clear stale pages/list cursors and stop scanning, preserving the existing
  pending-result and automatic-mode behavior. Socket backpressure and input interruption
  remain in Runtime. Door entry pauses the reader; it does not generate a
  pending-read cancellation or alter the existing Door/PTY contract.

Values are owned by one Session event loop, have no global state or allocation,
and must not alias output snapshots. The core does not save checkpoints or mark
articles read. Actual checkpoint persistence, monotonic timestamp enforcement,
member validation and storage failures remain in the existing Member Core.

## Compatibility

- P during SCAN reverses the existing direction and starts beside the current
  item; L repeats it. +/- uses the displayed item and the existing ID limit.
  Article and mail menus share this calculation without replacing filters.
- The original boundary responses remain: P/L boundary retains scan mode;
  relative/continue exhaustion ends it, with the mail-specific marker where
  previously used. F, STOP and pager handling retain their prior UI behavior.
- Stop while a worker request is outstanding suppresses its sequence result.
  The Session identity/slot guard runs before consuming that cancellation.
- CHECK preserves its selected board/scope/path and start-time upper bound.
  Articles start at cursor 1; mail/files at 0. End-of-page and completion are
  distinct, including empty continuation pages.
- AUTO commits only after the last page, output drain, and no pending request,
  displayed page or closing state. MANUAL requires DONE. A save error disables
  AUTO and retains the window for an explicit retry. Successful saving ends
  the traversal; cancellation does not request a save.
- Identity reset keeps the existing reset semantics and automatic preference.
  UI transitions, error markers, protocol formats and storage are unchanged.

## Validation

`make test-traversal-core` joins existing `make check`: ID/distance boundaries,
repeat/reverse/relative directions, delayed cancellation, independent states,
all target scopes, path bounds, frozen windows, failed-save retry and all 64
AUTO barrier combinations per target. The marker decoder is regression-tested
against its existing successful and malformed marker behavior.

Session adapter tests cover retained filters, article/mail boundaries, stale
completion rejection, stopped output suppression, Door pause, checkpoint
pagination, premature DONE rejection, failed/successful saving, cancel and
revocation. Existing `test_extended.py`, `test_mail_reading.py`,
`test_reading_settings.py`, stage64/65/67/68 reading and checkpoint tests remain
in the existing hosted CI suites. No home VM receives unreviewed PR code.

Local Windows validation executes the direct Core test and checks Session and
adapter syntax plus message catalogs. It does not claim runtime integration;
that remains hosted PR CI, then trusted-main native CI after merge. Canonical
manual generation, offline parity and internal/legacy links are checked too.
The first syntax check found three old field accesses in classic_commands.inc;
these were updated and the focused checks passed. No tests or limits weakened.

## PR CI correction

Head `8c17b924ea2f410cb20bdb415e2701edce3051b8` failed eight hosted jobs
at the same `src/runtime.c:191` reference to the removed `Session.scanning`
field. Local Session-only syntax checks had not compiled this Runtime path.
The board-refresh invalidation now calls a Session API retaining the original
page, view, report and reader reset behavior. Adapter regression checks also
assert that pending state, automatic-mode tick behavior and scan filters are
preserved. The former-field reference scan covers all source and tests.
Runtime compilation and adapter execution must pass the normal hosted CI;
local syntax checks do not replace them. No CI condition or test is disabled.
