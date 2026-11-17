# Member rights and level Core boundary

Baseline: main `5b96203d448834bd80261c18c6afb6b214d724eb` (PR #77).
This LN-23/24 slice extracts RIGHTS queries and LEVEL updates. GROUP/UNGROUP,
delegated SIGOP management, application details, lifecycle and authentication
remain in their existing paths. No new public command or storage format.

## Queries and authorization

member_core_rights accepts a trusted MemberActor and an explicit target ID.
The existing owner worker supplies identity/epoch; the service checks member
and moderation health, identity, suspension and epoch before reading rights.
Guests are rejected. Ordinary members may read only their own rights; global
administrators may read another active account. It does not infer authority
from a member level or a caller-provided admin flag. Target zero is not an
implicit self alias in Core; the protocol adapter resolves the existing default.

MemberRights owns its ID, level, 100 group flags and state. Indexes 0..99
represent groups 1..100; no private profile or credential is exposed. Pending
status retains precedence over suspension. Deleted/missing targets fail. A
bad level or store error clears the entire output. The snapshot describes
stored rights and must never be used as an authorization token.

## Durable level changes

member_core_level requires a current global administrator and an active target,
then checks level 0..5 before conversion to the existing signed store argument.
It passes the existing low group word to members_rights; the store preserves
higher group words. Level changes do not approve pending users or clear a
suspension. Existing policy permits administrators to change their own level
and protected accounts' levels; suspension-specific restrictions are not added.

The member store owns append/sync and durable actor attribution. Core advances
the credential epoch once only after a changed result, and returns MemberChange
for the runtime's existing notification path. The adapter no longer increments
the LEVEL epoch. Same-level requests succeed with a zero change result, without
another record, epoch update or forced reconnection. Failed writes/state checks
return no successful change notification. Self-updates also invalidate the old
actor epoch. UI code does not close sockets here.

All outputs own their storage, clear on failure and retain no caller pointers;
input and output buffers must not overlap. The service remains worker-owned.
RIGHTS text formatting stays in board.c, with its existing numeric group list,
status text and error responses. The unrelated GROUP/UNGROUP SIGOP authority
and credential-change path remain unchanged.

## Validation and limitations

Local C11 UCRT GCC tests with -Wall -Wextra -Werror pass all 14 Member Core
groups. Added cases cover self/admin rights, guest/nonadmin/revoked actors,
owned snapshots, pending status, deleted IDs, corrupt metadata, levels 0..5,
all group-word boundaries, same-level requests, self/protected updates, write
failure and single epoch publication. These are mocked persistence tests, not
native fsync or live-session evidence.

tests/test_rights.py adds cross-member query rejection, same-level journal-size
and live-session preservation, and groups 33/100 surviving level changes.
Its syntax passes locally; actual socket execution remains for existing hosted
PR CI and trusted-main native CI. The existing suite still checks restart,
access control, failed persistence and Japanese/English UI. No CI weakening or
extra full regression run is introduced.

Reviewed docs/manual/member-admin.md and Japanese/English adminmembers HELP:
commands, levels and groups remain accurate. CHANGELOG, this internal contract
and reviewed implementation digests are updated. Canonical manual generation,
links, search and Markdown/HTML/offline parity are validated separately.
No production access, deployment or release publication.
