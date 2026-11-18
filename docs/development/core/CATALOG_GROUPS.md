# Catalog group management Core

Baseline: main `6b976910736f502f79696a9d3db55676d5568ea3` (PR #101).
Its 11 workflows / 25 checks passed before this change; that is not a result for
this branch. No production, VM configuration, release or deployment is involved.

## Scope and existing behavior

`catalog_group_core.c/.h` extracts SIGOPGROUP and BOARDGROUP admission and
credential invalidation from the board feature dispatcher. Existing individual
GROUP/UNGROUP and global GROUPMOVE services in Member Core are reused unchanged,
not implemented a second time. The adapter owns command syntax, confirmation,
legacy messages, catalog refresh, vault notifications and session actions.

Both extracted mutations require the shared `catalog_core_admin` check of current
member identity, epoch, member/moderation/policy/catalog health and global admin.
A SIGOP's local or inherited role is never enough. Group records accept 1..100
only when present in the target board's own read/write ACL. The internal API
retains the old worker's zero-as-clear path, but the terminal number parser
rejects zero before dispatch; no terminal clear command is introduced. Migration
requires distinct nonzero groups and a source in that ACL.

The existing delegated table is a recorded association, NOT an additional
permission gate for GROUP/UNGROUP. `member_core_group` currently uses actual
catalog_sigop + catalog_group and protected-target checks. This refactor preserves
that policy; requiring the association would be a separate behavioral change.
The manual and both HELP languages now explain the distinction.

BOARDGROUP changes only the selected board's ACL, but migrates memberships
globally. Another board still using the old group can therefore become
inaccessible to those members. Group merging, IDs, retired slots and existing
membership journal semantics are not altered.

## Storage and lifetime

`catalog_group_repository.h` is internal to the single board worker. The exact
implementation in `catalog_group_store.inc` is shared with the SQLite unit test.
Existing delegated/group_moves schema, UPSERT/INSERT OR IGNORE statements and
transaction boundaries are preserved. There is no new DB format or migration.

The command persists an intent before invoking recovery. Recovery updates the
catalog ACL, then the membership journal, then atomically moves the board's
delegation record and marks the intent done. ACL/member changes are outside that
last SQLite transaction: a failure can leave durable partial progress. It is
incorrect to claim rollback of the whole operation. The existing recovery logic
replays pending work on startup and skips done intents. Successful duplicate
commands with an old source no longer present in the ACL are rejected.

Recovery failure after an intent sets the existing features_broken flag and
returns no success. Write failures before intent storage do not invoke recovery.
Startup recovery does not emit live-session effects. Internal replay is for
trusted persisted intents, not an unauthenticated API exposed to clients.

After a successful live migration Core advances every credential slot from 1
through the captured member count, including vacant slots, matching BOARDGROUP's
existing behavior. It returns an owned `{through, actor_epoch}` notification.
The adapter notifies vault for that inclusive range and requests revocation of
all other sessions while retaining the initiating session's updated epoch.
This differs from GROUPMOVE's existing named-member loop, which is not changed.
Failures clear the result and never publish successful refresh/credential effects.
All operations run on the existing serialized worker; no new lock/thread/queue.

## Verification

- Seven direct Group Core groups: admin/current identity, ACL/group bounds,
  explicit clear, durable failure, output clearing and notification ordering.
- Eight exact-repository SQLite groups: duplicate association, scoped clear,
  saved intent ordering, recovery replay, destination merge, ACL/member partial
  progress, rollback of delegation+done after an injected trigger failure,
  read-only/schema errors and closed storage. External ACL/member journals are
  stubbed here; SQL itself is real SQLite.
- Existing Catalog Core and Member Core test executables pass unchanged.
- Modified board translation unit and Group Core pass C11 warnings-as-errors
  syntax checking. A local Windows-only header declares the existing opaque
  flock pointer and POSIX lstat prototype; it is neither linked nor shipped and
  does not establish Windows runtime support.
- New socket test in test-stage65/make check covers journal and SQLite data,
  admin/SIGOP separation, target-only ACL vs global membership effects, other
  session revocation, initiating-session continuity, unchanged article data,
  restart and an injected stopped-copy intent recovered across two restarts.
  Runtime execution awaits hosted CI; native CI is trusted-main-only.

Existing stage64/member tests remain enabled. New Core and SQLite tests are
part of make check without changing workflow triggers, protection or timeouts.
Canonical manual/HELP changes are source-reviewed and generated/linked/offline
validated. Full LN-23/24 modularization remains ongoing.

## PR CI correction

Initial head `eda54ac5bf20d2b22764ab4879969c22bdadd394` failed the new
socket test on both Rocky Linux 9 and AlmaLinux 9 in
[Linux compatibility run 44232947541](https://github.com/matsuotakeshi/liner-notes/actions/runs/44232947541).
`SIGOPGROUP 3 0` returned `ERR command` instead of the fixture's expected success.
Baseline and current session.c use the same number_arg helper, which rejects
zero. The fixture and public HELP/manual incorrectly generalized an internal
Core/worker zero-as-clear path into a terminal command.

The socket fixture now requires the exact rejection and unchanged delegation
rows. Internal clear coverage stays in the direct Core and real-SQLite tests.
The public instructions and API comment distinguish the two boundaries. No
parser/runtime logic, permission, timeout or CI gate is relaxed. A local check
of the actual number_arg implementation confirms zero/00 rejection and positive
values; full socket revalidation remains pending hosted CI.
