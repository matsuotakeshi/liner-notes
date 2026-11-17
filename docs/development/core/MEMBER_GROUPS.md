# Member group Core boundary

Baseline: main `c9fff908b128c30cb4e2a485b181ed02e36a7e7e` (PR #78).
This LN-23/24 slice extracts GROUP/UNGROUP. GROUPMOVE, BOARDGROUP recovery,
SIGOP assignment and all other account/board operations retain their existing
implementations. There is no new public command, privilege or storage format.
The next [account lifecycle slice](MEMBER_LIFECYCLE.md) extracts terminal account transitions.

## Authority and dependencies

member_core_group accepts a trusted MemberActor, board context, explicit target,
group number and add/remove flag. The worker supplies the actor identity and
epoch; Core rechecks identity, store health, suspension and epoch on every call.
Guests are rejected before any target disclosure. Input/output buffers must not
overlap, outputs own their storage, and failure clears the change result.

Global administrators can manage any active target regardless of board context.
Other users require both catalog_sigop(board, actor) and catalog_group(board,
group), and cannot modify a protected target. These are the same decisions as
the old board adapter helper. Catalog retains parent-SIGOP inheritance and
checks board existence; group membership in the ACL is tested on the selected
board's own read/write ACL, not inferred from arbitrary ancestor group rules.
The API receives no caller-provided delegation boolean or cached authority.
A change of SIGOP assignment or ACL is effective on the next call.

This adds a dependency on the existing catalog authority API, not on Session,
UI or protocol code. It does not need password grants: the previous management
rule does not use article-read password authorization. The board adapter's
existing policy/feature storage health gates remain intact. This is not a
complete extraction of catalog/board-policy lifecycle or recovery management.

Core requires an active target, group 1..100 and add=0/1 before persistence.
Unauthorized SIGOP requests preserve the admin-required response, including
protected targets and groups outside the selected ACL. Global admins retain
the ability to manage protected accounts. Pending/suspended status and level
are not altered by changing group membership.

## Persistence and adapter

members_group_change remains responsible for durable membership records, actor
attribution and all four group words. A changed result advances the credential
epoch once and returns MemberChange for the runtime's existing invalidation
path. Same-state requests return success with zero change, no new journal entry
and no epoch increment. Failed writes publish no success/epoch notification.
Self-changes invalidate the caller's old epoch as before.

board.c delegates G/D, renders the same RIGHTS reply and forwards the result;
it no longer performs a second group write or epoch update. The obsolete
features64.inc group_delegated helper is removed. Unrelated recovery code stays
unchanged. Makefile adds catalog.h to the existing Member Core test dependencies.

## Validation

Local C11 UCRT GCC tests with warnings as errors pass all 17 Member Core groups.
New cases cover every group 1..100, add/remove no-ops, untouched memberships,
actor attribution, live delegation/ACL changes, exact board/group scope,
protected targets, self revocation, invalid arguments and failed writes.
Catalog and persistence are mocked; these tests do not prove actual ancestry
traversal, disk sync or native connections.

The existing tests/test_stage64.py sets a parent SIGOP and operates on a child.
It now also checks delegated removal, repeated add/remove journal sizes,
rejection from another current board and protected-target removal rejection.
Python syntax passes locally. Real socket execution stays in existing hosted
CI, then trusted-main native CI; no duplicate full local regression is run.

Reviewed the canonical member-admin and board-admin instructions and Japanese/
English adminmembers HELP. User-visible rules remain accurate; CHANGELOG and
this internal contract document the extraction. Only reviewed tracked source
hashes are updated. Canonical manual generation, links, search and offline
parity are validated. No production access, deployment or release publication.
