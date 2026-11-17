# Member lifecycle Core boundary

Baseline: main `3663a2a8f553d5b38da97f4ee03730d8d1595a73` (PR #79).
This LN-23/24 slice extracts REJECT, DELETEAPPLICATION, RETIRE and DELETEUSER.
Registration, authentication, password changes, application editing, physical
compaction and other member operations keep their existing implementations.
The subsequent [password service](MEMBER_PASSWORD.md) extracts password changes and resets.

## Authorization and operation kinds

member_core_remove runs on the existing owner worker with a trusted MemberActor
and an explicit operation kind: reject=1, delete application=2, retire=3,
delete member=4. Core rechecks identity, suspension, credential epoch and member/
moderation store health. UI input never supplies an administrator privilege.

Only retirement is available to an ordinary authenticated member. Core always
resolves that target from the actor, ignoring any supplied target; a forged
other-member target cannot retire somebody else. All other kinds require a
global administrator. The target must be active. Protected accounts are rejected
for every kind, and non-retirement operations cannot target the caller. The
existing guest responses are retained: no member for RETIRE and admin required
for the administrator operations. Unknown operation kinds fail without writing.

The existing member store owns pending-state validation: reject/delete application
require pending accounts, while retirement/delete member require nonpending
accounts. Repeated removal is rejected because the account is no longer active.
This is separate from reversible SUSPEND/RESUME. The Core service does not
expand SIGOP authority or add any cascading article/mail deletion.

## Persistence, identity and session lifetime

members_remove remains unchanged. It records the terminal kind, actor and time,
publishes only after durable append, and clears stored password hash/salt in
memory. IDs and login names remain reserved; no count reduction or renumbering
is introduced. Physical compaction and old-backup handling are outside this API.

Only after a successful store result does Core advance the credential epoch
once and return MemberChange. board.c forwards that committed user/epoch to the
existing runtime invalidation path and formats the unchanged MEMBER REMOVED
response. Core does not close sockets or retain Session pointers. The adapter
no longer performs its own duplicate epoch update for lifecycle operations.

Failed writes return storage failure and no successful change notification;
state mismatches return the existing member-state error. All output bytes are
cleared on failure, input/output buffers must not overlap, and no pointer is
retained. Removed actors remain revoked even if supplied the new epoch.

## Validation and limits

Local C11 UCRT GCC tests with warnings as errors pass all 20 Member Core groups.
New tests cover all four transitions, actor attribution, retained identity/count,
self-only retirement despite a different target, protected targets, revoked/
unauthorized actors, pending-state mismatches, invalid kinds, repeats and failed
writes with no epoch publication. The persistence mock models terminal states;
it is not evidence of real credential erasure, disk sync or native sockets.

Existing tests/test_stage63.py covers actual deletion/retirement, old-session
rejection, pending application removal, restart, reserved IDs/names and offline
backup/compaction. It now also verifies repeated deletion/rejection leaves the
member journal size unchanged. Syntax passes locally; actual execution remains
in existing hosted PR CI, then trusted-main native CI. No redundant full local
regression or weakened CI condition is introduced.

Reviewed canonical accounts/member-admin instructions and Japanese/English
account-admin HELP. Existing user-visible instructions remain accurate.
CHANGELOG, this internal contract and reviewed implementation digests are updated.
Canonical manual generation, links, search and Markdown/HTML/offline parity are
validated separately. No production access, deployment or release publication.
