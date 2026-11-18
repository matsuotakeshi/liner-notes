# Batch selection and individual completion Core (LN-23/24)

Unreleased refactor after #118. The local selection, download acknowledgements,
copy destination and copy progress form one `BatchSelection` instead of separate
Session arrays. There is no new transfer protocol or persistent batch format.

## Responsibility and ownership

- `batch_core.c/.h`: ordered selection, duplicate-add/missing-remove no-ops,
  effective capacity, per-ID download completion, replacement, pending snapshots,
  destination-scoped copy continuation and full reset. No UI, filesystem, DB,
  worker, membership or transport dependency.
- `batch_protocol.c/.h`: existing worker response ID-list grammar, bounded CSV
  formatting and helper DOWNLOADED receipt decoding. MATCH and MAILBATCH retain
  valid-prefix parsing and the 16-entry ceiling. This trusted-worker grammar is
  deliberately separate from user-command validation and grants no permission.
- Session: login/feature/input checks, runtime quota lookup, protocol capability
  checks, localized output and dispatch to authorized vault/mail workers.
  Selecting a number does not authorize access. Workers still verify each file,
  destination, quotas and member state before operations and history recording.
- Runtime: verifies live connection, slot, session ID, active helper and matching
  transfer generation before calling `session_transfer_receipt`. The Session API
  only updates file-selection receipts for vault batch download mode (2); mail
  attachments and uploads do not mark vault IDs. No generation checks moved or
  weakened, and Runtime no longer edits batch arrays directly.

BatchSelection is owned by one Session and mutated only by its existing event
loop. Initialize/reset before use; do not alter the selection while a copy worker
is pending. BatchIds snapshots are small independent values. Replacement input
must not alias selection storage. The Core uses no heap allocations, locks or
shared global state. Completed worker results still pass Session's identity gate.

## Failure and compatibility behavior

- Download success marks only matching selected IDs; repeat receipts are
  idempotent. Overall transfer failure never marks remaining files complete.
- SEND formats only pending IDs in original order. Removing/re-adding creates a
  pending entry; existing flags follow their IDs through array compaction.
- Copy success advances past already copied entries. Copy failure does not call
  the success transition, so the same destination retries the failed entry.
  Changing destination clears copy receipts only, retaining download receipts.
- MATCH replaces selection and resets both receipt sets. The remembered copy
  destination remains local. CLEAR, identity changes and revocation reset the
  selection. Unused backing bytes are cleared; no saved data is changed.
- Mail attachment batch lists reuse the ID-response formatter, not vault
  selection state. Existing mode/protocol and downstream authorization remain.
- A protocol formatter failure returns empty output, never a truncated success.
  Current fixed list buffers fit all 16 maximum-size IDs.

## Validation

`make test-batch-core` joins existing `make check`. It checks reduced/fixed limits,
duplicate and missing operations, per-ID flag alignment, remove/re-add, invalid
copy destinations, interrupted copies, destination changes, independent sessions,
MATCH replacement, parsing/format boundaries and all 65,536 subsets of 16
completed downloads (reverse/repeated acknowledgements).

Session adapter coverage adds guest denial, mode-specific helper acknowledgements,
partial failure retention, displayed DONE/PENDING state, stale completion
rejection, MATCH replacement, copy failure and revocation reset.
Existing `tests/test_stage68_batch.py` tests actual Telnet partial completion and
retry; existing batch/copy/mail-transfer suites remain enabled. Runtime's helper
generation gate is unchanged. Hosted PR CI provides actual runtime integration;
local Windows syntax checks are not claimed as runtime execution. Native OS and
package checks remain trusted-main only. No production connection, release,
new protocol or change to transfer accounting is part of this PR.
