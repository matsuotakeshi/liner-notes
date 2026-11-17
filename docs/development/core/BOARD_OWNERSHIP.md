# Board ownership and management Core boundary

Baseline: main `ef155d3c1ea713092111b50b43f257c8a092e3a0` (PR #97).

## Scope and contracts

`article_author_set`, `article_guest_set`, and `article_sigop_set` are worker-only
Article Core operations. They take authenticated ID/epoch, never a caller-supplied
admin flag. They recheck member/moderation/policy/extras health and current identity.
No UI, session, queue or transport types occur in their contracts.

- AUTHOR finds the actual article's board, rejects effectively concealed articles,
  and checks current manager authority, independently of the displayed board.
  The destination must be active, not pending or suspended. The existing A audit
  record is durable before the worker updates author metadata and both post counts.
  Imported articles retain their origin identity; this changes only their existing
  local displayed-author override, not origin ownership or link events.
- BOARDGUEST validates management scope and the boolean value. Its repository hook
  stores the existing guest_boards setting; only successful persistence publishes
  catalog state. No ACL/password override or new settings schema is introduced.
- SIGOP requires administrator or an ancestor SIGOP, not merely the target's own
  direct SIGOP. Catalog retains its durable validation as a second boundary.
  Result values own previous/current member IDs and epochs. No-op assignments do
  not invalidate credentials. Failure clears the result. Session account updates
  remain in the activity adapter; Core owns credential epoch invalidation.

The board worker serializes repository state, so no new locks or API lifetimes are
introduced. Repository author publication is called exactly once after A commits.
The features/activity adapters format legacy responses and refresh session/catalog
hints. Existing wire commands, schema, journal format and messages remain unchanged
for valid sessions. Direct stale/guest calls fail closed.

## Verification

- MinGW C11 `-Wall -Wextra -Werror`: Article Core direct suite, 22 PASS groups.
  Four added groups cover authorization, ancestor delegation, target validation,
  storage failure, no-op/removal epochs, durable publication and failure outputs.
- New `tests/test_board_ownership.py` is in existing `test-stage65` / `make check`:
  parent/child/outside scopes, self-replacement denial, role removal, author audit,
  old/new author quotas, same-author reassignment, hidden rejection, restart and
  guest-policy persistence. This real-socket test is pending hosted CI, not claimed
  as locally executed on Windows.
- Windows syntax-only compilation of activity.c reaches the existing POSIX
  struct flock declaration incompatibility in storage_sqlite.h. This is not a
  passing runtime build; hosted Linux and trusted-main native CI remain required.
- Manual board-admin description reviewed against catalog_role_set/catalog_manage
  and existing AUTHOR implementation; HELP/command syntax stays unchanged.

No production, VM changes, migration, package publication or release performed.
