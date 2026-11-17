# Member mail notification Core boundary

Baseline: main `6c74a05c02aa83265f57a605f2d8b791f2ee7fd5` (PR #82).
This LN-23/24 slice extracts MAIL NOTIFY query and ON/OFF into Member Core.
Mail delivery, mailbox ACLs, login initialization and runtime/session notification
handling remain unchanged. No command, schema, journal or package change.

## Authority and completion contract

member_core_mail_notify accepts a MemberActor, a QUERY/ON/OFF enum and caller-owned
MemberMailNotify output. There is no target-member argument: both query and update
apply only to the authenticated actor. Core rechecks member/moderation store health,
identity, suspension and credential epoch before reading or writing preferences.
Guests and revoked actors cannot obtain another user's notification state.
Null output and unknown actions are rejected. Output is zeroed on any failure.

The success snapshot contains user ID, deny flag and notification revision.
QUERY and same-value updates preserve the revision; only a durable changed value
advances it. Default state is notifications ON, revision zero. The existing store
handles duplicate suppression and rejects a changed value at UINT32_MAX without
wrapping; querying or setting the same value still succeeds at that boundary.
Credential epochs are separate and never changed by notification preferences.

Core owns no socket or Session and retains no output pointer. The existing
serialized board worker is required; this API adds no independent locking.
board.c retains wire-number validation, localization and mailbox-health gating.
Only successful completion copies the snapshot to BoardResult. Existing runtime
broadcasts it to all connections; session_mail_notify applies it only to matching
members and ignores older revisions. OFF clears queued arrival notices; ON does
not replay arrivals received while disabled. Those delivery rules are unchanged.

## Failure and persistence semantics

members.c continues to own the N journal record and append-before-publication.
Write failure returns member-storage error without a success snapshot or runtime
notification. A readback failure after successful persistence also clears output,
but may leave the new setting committed. This is not a rollback guarantee.
The adapter clears mail_action on failure and reuses existing member error mapping.
Login still restores notification preferences using its existing initialized path.

## Verification

All 29 direct Member Core groups pass with local UCRT GCC, C11 and warnings as
errors. Three new groups cover default/query/ON/OFF, duplicate writes, per-user
isolation, stable credential epochs, guest/revoked/retired/pending actors, invalid
arguments, failed stores, partial reads and revision saturation. Storage is mocked;
these tests are not real journal/fsync or multi-session integration evidence.

The existing test_mail_notify.py has an additional OFF-query/no-write assertion.
Python syntax passes locally. Existing runtime coverage includes persistence,
restart, real duplicate writes, live peer propagation, canceled queued notices,
no replay on ON, Japanese/English UI, PREFS independence and damaged/partial
journals. It remains in make check for hosted CI and trusted-main native CI; full
regression and native runs are not duplicated locally.

Reviewed canonical mail/personal-settings documentation and Japanese/English mail
HELP against members.c, session.c and runtime.c. Public semantics remain accurate;
the stable manual version is unchanged. CHANGELOG, this internal contract and
reviewed source digests are updated. Canonical generation, links, search and offline
parity are verified separately. No production access, deployment or release.

The following [chat removal slice](MEMBER_CHAT_KICK.md) extracts a separate
admin-authorized moderation order without changing notification preferences.

The later [mail composition boundary](MAIL_COMPOSITION.md) extracts composition,
draft/send/delete and attachment selection; MAIL NOTIFY remains Member Core.
