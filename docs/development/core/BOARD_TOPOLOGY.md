# Board topology Core boundary

Baseline: main `1bcda0c7fea0c6445e68350ca1c5fac2f6145f25` (PR #100).
Its 11 workflows / 25 checks passed before this change. This is not evidence
for the new branch. No production or home-VM settings were changed.

## Responsibilities and contracts

`board_topology_core.c/.h` owns empty/cascade board deletion, whole-thread/branch
move admission, and shared count/byte quota admission. The worker supplies the
trusted `CatalogActor` and serializes the entire operation; callers cannot pass
an administrator flag or construct a storage plan through these public services.
`catalog_core_admin` reuses Catalog Core's member/storage/epoch checks and then
requires the actual administrator role. The source's managed scope is checked
for empty deletion/moves, and SIGOP scope alone never grants these operations.

UI/session adapters keep command parsing, CONFIRM tokens, prompts, selected board,
refresh notifications and legacy protocol text. Core imports no Session,
BoardRequest or terminal renderer. The internal capacity helper is not an
ACL-authorized API for displaying another board's contents or usage.

Repository hooks copy only board/parent/root/body-size/removal metadata from the
single-owner index, with no transient db_at pointers escaping. Core selects the
move scope and checks quotas; it supplies one existing M/S journal event to the
store. Store append/sync, replay, index/link rebuilding, historical locations,
visibility cache invalidation and restart replay remain unchanged. Branch moves
detach only the selected root; descendant parents, article IDs and remote
provenance are not reassigned. All descendants are moved, including hidden and
withdrawn ones; only non-withdrawn rows count against destination quota.

Cascade Core snapshots the complete subtree and depths. The repository records
that plan in the existing SQLite transaction before existing deepest-first
`delete_recover` and access-log pruning. No new schema, transaction boundary,
thread, transport or on-disk representation is introduced. The plan is bounded
by CATALOG_MAX; capacity and move scans use bounded per-record metadata.

## Ordering and failure behavior

- Empty-delete/move preserve managed-source -> format preparation -> admin
  ordering. The pre-existing format preparation may occur even for a SIGOP
  whose destructive operation is subsequently denied.
- Cascade checks admin before target/root validation; failed legacy format
  preparation still maps to ERR command for this command only.
- Empty deletion checks all articles (including withdrawals) and live children.
- Quotas preserve zero=unlimited, hidden-included, withdrawn-excluded semantics.
  The shared posting/import wrapper treats uncertain metadata as no admission.
  Byte accumulation checks overflow; corrupt non-decreasing parent IDs fail
  closed rather than hanging branch traversal.
- Empty deletion or cascade recovery may fail after durable changes. No rollback
  is claimed. The original broken-storage flags and restart recovery remain.
- Move acknowledgement occurs only after journal append and replay succeed.
- Scans retain the existing linear quota / ancestry-walk strategy; this is not
  a claim of indexed quota aggregation or constant-time branch operations.

## Validation

Local C11 -Wall -Wextra -Werror checks:

- Board Topology: nine groups covering quota boundaries, move selection,
  invalid targets, destination capacity, corrupted/read-failed metadata,
  empty/cascade deletion, storage failures, SIGOP denial and identity revocation.
- Catalog Core: seven groups, extended for the administrator entry point.
- Catalog Policy Core: seven existing groups remain successful.
- Full modified board translation unit plus new Core and Catalog Core pass
  syntax checking. The Windows-only test header declares the pre-existing
  opaque struct flock and unavailable POSIX lstat prototype; it is not linked,
  installed or a Windows runtime implementation. No warnings are disabled.

`test_board_topology_core.py` is added to existing test-stage65/make check for
journal and SQLite sockets: authority denial, quota failure, hidden/withdrawn
moves, parent detachment, IDs/seen state, deletion/cascade and two restarts.
Existing stage60, stage65 and stage65_delete_recovery remain enabled, including
the established interrupted-deletion recovery coverage. Socket execution and
native/package regression await existing CI; no new environment is created.

Canonical manual source and source-lock are reviewed with the unchanged wire
commands and storage layout. Public HELP is unchanged because no command or
permission contract changed. This branch does not complete all LN-23/24 work.
