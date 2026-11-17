# Reading checkpoints and menu guidance Core boundary

Baseline: main `58f1e4c0f98a338bab10d9a2726f9141ed0482d0` (PR #96).
This grouped LN-23/24 extraction covers account-owned reading checkpoint windows,
explicit completion commits, and persisted menu guidance selection/restoration.

## Core and persistence contract

- `member_core_menu_get/save` accepts the authenticated actor and the three
  existing BEGINNER/STANDARD/EXPERT values. No target member or administrator
  override is accepted. Guest session-only selection remains with Session.
- `member_core_check_begin` returns an owned `{since, until}` window. It reads
  the actor's last committed timestamp and captures the server time, without
  writing anything. Failed reads or clock failures clear the complete output.
- `member_core_check_commit` rechecks identity/epoch/suspension and storage health,
  validates the scope and rejects future or out-of-SQLite-integer timestamps.
  The existing SQL `max(stamp, excluded.stamp)` preserves monotonic commits when
  concurrent sessions finish in reverse order. Repeated completion is idempotent.
- Kinds remain ARTICLES=0, MAIL=1 and FILES=2. Empty scope is the existing global
  key. Article scopes are canonical `board:0` (HERE) and `board:2` (SUBTREE), with
  a valid configured board-number range. File scopes retain the existing path
  strings up to 48 bytes, including non-ASCII text. Mail has only the empty scope.
  These keys select checkpoints; they never authorize board/file/mail access.
  Content services continue to check current ACLs on every page.
- If the clock moves backward, an existing later checkpoint is not silently
  reduced. A captured completion later than the new server time is rejected;
  normal retry and recovery do not overwrite it with zero.

The trusted Session/board worker supplies the actor and the captured completion
time. Core does not accept a remote privilege flag. It is not a new externally
callable completion-token service: Session still tracks whether traversal ended,
AUTO/MANUAL selection, cancellation/disconnection and the pending commit. Other
UIs must retain an equivalent trusted traversal state before requesting commit.

`member_settings_repository.h` separates these operations from SQL. The existing
board-owned SQLite connection is used by `member_settings_store.inc`; the same
implementation is included by an isolated real-SQLite test. No additional
connection, thread, database or schema is introduced. The three existing tables
(`menu_preferences`, `news`, `news_scoped`) and keys remain unchanged.

Missing rows are valid defaults (STANDARD, timestamp zero). SQL preparation,
binding, stepping/finalization failures and invalid stored types/ranges return
failure instead of masquerading as absent rows. Queries bind values and finalize
all statements. SQLite keeps the existing atomic upsert and connection durability
settings. Optional menu retrieval failure at login uses STANDARD for that session;
it does not acknowledge a saved preference or prevent otherwise valid login.
Explicit PREFS LOAD and CHECK return the existing settings-storage error.

## Preserved behavior and remaining boundaries

board.c/features64.inc handle the existing command/completion fields and text.
Session keeps menu rendering, temporary guest mode, CHECK paging and the decision
to commit only after traversal. New API results never grant content visibility.
The existing Session behavior of applying a requested menu mode before its save
reply is unchanged; a save error still means persistence has not been confirmed.
Article read flags, other display preferences, password epochs and drafts are
not modified. Guest-board configuration remains in its existing store helpers.

No public command, HELP token, protocol, package dependency or migration changes.
The public manual clarification describes already-existing scope separation and
monotonic completion behavior. This does not complete all Session/Core extraction.

## Verification

- C11 `-Wall -Wextra -Werror`, local MinGW GCC: all 58 Member Core groups passed,
  including four new groups for kind/scope windows, key/time boundaries, direct
  actor/health gates, menu modes, partial results and persistence/clock failures.
- Three real-SQLite groups passed against the exact included repository code:
  defaults and owner/kind/scope isolation, bound SQL text, monotonic/idempotent
  writes, signed integer limits, rollback, read-only rejection, malformed/missing
  tables/values and statement cleanup. No live BBS or production data is used.
- `tests/test_reading_settings.py` is added to make check for real sockets and an
  isolated SQLite database: menu persistence, scoped checkpoints, cancel and RAW
  disconnect, restart, and deliberate malformed/missing settings data. Runtime
  execution is pending hosted CI; syntax checking alone is not success evidence.
- Existing hosted checks remain required. Native CI executes only the exact
  trusted main SHA after merge. No PR code is sent to home VMs or production.
- Local Python syntax, reviewed source fingerprints and diff checks passed.
  Canonical manual generation and validation passed: 42 pages, 2,812 local
  links, 172 legacy URLs, Markdown/HTML parity and offline ZIP.
