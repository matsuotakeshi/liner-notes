# Catalog management Core extraction

Baseline: main `2e77f813b772faa004aa05e32e7e4b3c64e7028e` (PR #98).
All eleven workflows succeeded before this work, including Multi-OS Native CI
44149447963, Solaris 44149447971, package workflows, Release Set 44150153325 and
FreeBSD isolated upgrade 44150153427. These successes apply to the baseline only.

## Responsibility and data flow

- `catalog_core.c/.h`: authenticated actor/epoch checks, store health, current and
  proposed management scope, owned settings/role/history results, bounded history
  iteration and clearing outputs on failure. No UI, queue, socket or Session types.
- `catalog.c/.h`: catalog schema/ACL parser, durable overlay commit/replay, copied
  settings, direct assignment lookup and checksum-checked journal row reads.
  The previous terminal response renderers are removed from persistence.
- `activity.c`: legacy command request routing and response rendering only for
  BOARDINFO / BOARDSET / SIGOPS / BOARDLOG. Existing refresh service flags remain.

Calls run on the existing single board worker. No new locks, background tasks or
borrowed result lifetimes are introduced. Outputs belong to the caller. Inputs
must not alias outputs. Normal bounded snapshots require about 1 KiB for settings,
8 KiB for assignments or 8 KiB for a history page; nothing scales with log length.

## Compatibility and authorization

BOARDSET still takes the existing declarative six-field catalog specification.
This is not a terminal command passed through Core. The existing parser validates
schema, ACLs, cycles and bounds; Core rechecks authority over the current board
and any proposed parent, and the durable repository keeps its validation too.
Exact spelling is retained in the historical B record. This extraction does not
introduce a new schema, typed replacement wire format, migration or normalization.

All four APIs reject missing/stale/suspended actors before reading or committing.
SIGOPS permits ordinary members but returns only boards they can manage, so their
list can be empty. It returns at most CATALOG_MAX=1024 assignments in board-number
order. A zero assignee means no direct SIGOP, not lack of inherited management.

BOARDLOG retains the current-board admission rule, but each row uses current
management authority for its own target. It scans at most 256 candidates, returns
at most eight rows, preserves descending cursor conventions and clears all rows
on a later read failure. Empty pages with continuation remain valid. Broken
catalog/role journals fail closed across these management APIs; they are not
presented as an empty successful response.

## Verification and limitations

- `test_catalog_core.c`: seven direct test groups passed with MinGW C11,
  -Wall -Wextra -Werror. Covers data ownership, actual/proposed board scope,
  failure publication, maximum role list, pagination and revoke/error paths.
- Actual catalog.c, catalog_core.c and activity.c pass syntax-only checking with
  the same flags. The local Windows harness supplies only `struct flock;` for
  the existing opaque POSIX pointer declaration in storage_sqlite.h; no warning
  flags or product behavior are changed. This is not a native runtime build.
- `test_catalog_management.py` is included in test-stage65 / make check. It covers
  raw commands, inherited settings/roles, reparent rejection, multi-page filtered
  history, role revocation, journal/SQLite restarts and live temporary-journal
  corruption after valid history rows. Execution is pending hosted CI.
- Existing stage58/stage64/stage65 and board-ownership tests remain enabled.
- Public commands and HELP syntax are unchanged. The board-admin manual clarifies
  existing scopes and cursor behavior; Canonical manual validation is required.

No production hosts, VM configuration, release or deployment are touched.
