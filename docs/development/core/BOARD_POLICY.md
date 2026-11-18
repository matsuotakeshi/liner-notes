# Managed board policy Core boundary

Baseline: main `85dccfe32dc06afe70387fa22076d70d3fb74272` (PR #99).
PR #99's 19 checks passed before merge; that does not establish this baseline's
native result or this branch's CI result. No production/VM settings were changed.

## Responsibilities

`catalog_policy_core.c/.h` owns board-policy operations, using the shared
`catalog_core_manage` identity/epoch/storage/management check on every call.
The seven entry points cover options read/write, password prompt authorization,
password save/clear, guide read/write, and access-history pages. They do not import Session, BoardRequest,
BoardResult, socket or terminal types.

`board_policy.c` keeps parsing, password hashing, durable policy transactions and
replay. Its old terminal report is replaced by an owned BoardPolicySettings with
only public limits/order/menu state and a password-present flag. No salt, hash,
password revision or cleartext secret is returned. Borrowed password inputs are
never retained, copied to response objects or logged by Core.

`board_policy_repository.h` contains the single-worker storage hooks implemented
beside existing state in board_features.inc: format preparation, article presence,
forced retention and copied access rows. The queue still owns lifecycle and writes;
no new threads, locks, storage schema or data migration is introduced.

The board adapter retains prompts, editor state, catalog refresh and legacy wire
rendering. Input and output storage must not overlap. Snapshot/body/page outputs
are owned by callers and clear on failure. Normal snapshots use bounded memory;
no result holds journal pointers or article bodies for access-log viewing.

## Preserved ordering and limitations

- Options/password/guide saves still prepare RTBOARD5 before policy writes.
  A password prompt or guide read does not upgrade/write storage.
- MENU changes reject any article currently on that board, including withdrawn
  entries, as before. Options commit before forced retention. A prune failure
  marks access storage broken and suppresses successful refresh; it does not
  roll back an already durable policy record. Recovery remains an operator task.
- Guide write exposes an attempted flag only after authorization/format readiness,
  preserving the editor's existing successful-close vs failed-write behavior.
  The guide bound remains BOARD_INFO_MAX=8192 bytes; the general post body bound
  is independent. Chunked information journal semantics are unchanged.
- Access history preserves audit metadata and current per-board retention.
  Each page returns up to eight rows after at most 256 display candidates.
  Finding the oldest retained record still walks backward as in the old code;
  this refactor does not claim a constant-time retention query.
  A failed read during either traversal clears the whole result.
- Guest board unlocking, deletion, thread moves and access-record creation remain
  existing worker operations; they are not claimed as extracted by this change.

## Verification

- Direct Catalog Core: seven test groups passed, including shared-management
  authorization assertions. Policy Core: seven additional groups passed for
  snapshots, persistence ordering, passwords, guide boundaries/editor outcomes,
  retention/cursors, partial-read failures and authorization before side effects.
  Built with MinGW C11 -Wall -Wextra -Werror.
- Actual catalog/policy/activity C sources and extracted exact board-policy
  rendering functions pass syntax checks with warnings as errors. The Windows
  harness forward-declares only the opaque struct flock pointer type required by
  the existing storage header. Full board.c syntax checking still stops at the
  pre-existing POSIX lstat usage in features64.inc; no diagnostic was suppressed
  and no full Windows runtime build is claimed.
- Added test_board_policy_core.py to test-stage65/make check. Covers inherited
  manager scope, password/guide/settings, retention and role removal across
  restart, in both journal and SQLite configurations. Socket execution is pending
  hosted CI, with existing stage60 and stage64 tests retained.
- Public HELP/commands and defaults stay unchanged; board-admin manual clarifies
  existing options/read-history behavior. Canonical generation/link/offline
  validation is run before the PR.

Native tests remain trusted-main-only. No release or deployment is performed.
