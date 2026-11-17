# Member suspension Core boundary

Baseline: main `5f7d47d9fde957304e052db4c7119a3f5f0660fd` (PR #75).
This LN-23/24 slice extracts SUSPEND/RESUME decisions from the board adapter into
member_core. Passwords, approval/deletion, profiles and RIGHTS are unchanged.

## Contract and trust

`member_core_suspend(actor, target, suspend, out)` runs on the existing owner
worker. The authenticated actor ID/epoch is supplied by the trusted session;
the service rechecks identity, epoch, suspension, global admin status and store
health. It accepts no caller-supplied privilege, audit count or new epoch.

The target must exist, differ from the caller and not be protected by the
existing moderation policy. Only suspend=0/1 is accepted. The combined committed
moderation/extras audit count is checked with wide arithmetic before mutation;
at UINT32_MAX it fails, including no-op requests, matching the old adapter.

The existing moderation_suspend store owns durable append/sync, actor attribution,
state publication and epoch advancement. Core neither appends an additional
audit entry nor increments epochs a second time. Repeated same-state operations
remain successful without another journal record or epoch increment. Resume
only clears the suspension flag: pending approval and retired-account restrictions
remain effective. It does not approve or recreate accounts.

On successful persistence, MemberChange contains the target ID and its committed
credential epoch. The adapter forwards both to the existing runtime invalidation
path. On any failure the output is zero and no success notification is emitted.
A failed durable write reports moderation-storage uncertainty; the store's broken
state blocks further operations. Core never closes sockets or holds Session
pointers. No output pointer is retained; input/output buffers must not overlap.

## Adapter and session behavior

board.c retains SUSPENDED/RESUMED and the existing protected-member, no-member,
admin and storage error responses. The former outer x/y admin gate is now inside
Core so direct calls cannot bypass it. Unrelated h/r/i gates remain unchanged.

runtime.c still compares each connection's epoch with the committed completion
epoch, clears pending input and revokes stale sessions. A quick resume cannot
revive an earlier connection, even if its client has not yet read the suspension
notice. A fresh authenticated connection receives the new epoch. This slice
does not change runtime/session code or the journal/DB format.

## Validation

Local UCRT GCC tests compile with -std=c11 -Wall -Wextra -Werror and all seven
Member Core groups pass. Added cases cover suspend/resume, actor attribution,
idempotence, old-epoch rejection after resume, pending-account restrictions,
admin/identity revocation, self/protected/nonexistent targets, combined audit
capacity/overflow, failed appends and cleared failure outputs. The store mock
models publication after success, not real fsync or live connection behavior.

Existing test_suspension.py exercises real sockets, protected accounts, concurrent
draft/timer sessions, fast resume, restart, unchanged member/article data and
partial/corrupt moderation journals. It now checks that a repeated resume leaves
the actual journal length unchanged before confirming the old sessions close.
Python syntax is checked locally; runtime integration stays in hosted PR CI.
The Makefile adds the extras header to the existing member test dependencies;
no CI gate or test condition is weakened. Native CI stays trusted-main only.

Reviewed docs/manual/member-admin.md and Japanese/English adminmembers HELP;
existing operations and permissions remain accurate without public changes.
CHANGELOG and this internal contract describe the extraction. Source-lock updates
only reviewed implementation/build digests. Canonical manual generation, links,
search and offline parity are checked. No production changes or release.
