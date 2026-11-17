# Member profile mutation Core boundary

Baseline: main `9c123af73e4bd095761b5f6b1ae682678876aba4` (PR #87).
This LN-23/24 slice extracts own profile writes and visibility controls, including
BIOEDIT/shared-buffer commits and the legacy short field commands. Profile
reading/search, administrator editing, registration details and editor prompt
management remain with their existing implementations. No public command,
permission, default visibility, persistence format or package dependency changes.

## Authority and ownership

member_core_profile_set and member_core_profile_visibility accept a trusted
MemberActor plus structured field/value arguments. Neither accepts a target
account, caller-supplied administrator flag or cached compatibility policy.
Core checks member/moderation health, authenticated identity, epoch, suspension,
and profile/extras health. Guest, stale, retired and missing actors cannot write.

The live members_rtbbs_policy restricts a non-admin's birthday (field 8) and
address (field 11) edits. Global administrators may edit their own values. SIGOP
status does not bypass this restriction. Visibility changes remain available to
the owner in compatibility mode, including birthday month/day and private mode.
These restrictions now apply to direct Core callers as well as the board adapter.

Profile writes accept fields 0..12 and the shared buffer (13). Text is borrowed,
NUL-terminated and never retained or returned. NULL and unknown fields fail
before storage. Existing profile_store owns UTF-8/date/length validation,
multipart journal commits and actor attribution. Core does not duplicate those
validators or load private values back into a result structure.

Visibility excludes the shared buffer. Modes are private (0), members (1) and
birthday month/day (2). HANDLE must stay members-visible; mode 2 applies only to
birthday. Core checks this matrix before the defensive storage validation.
Results contain only status. There is no credential epoch update or connection
invalidation for profile changes. The existing serialized worker lifetime and
profile journal failure/replay behavior are unchanged.

## Adapter and failures

The board adapter retains command parsing, the 80/320-byte shorthand input
limits, editor completion and reply rendering. PROFILESET/BIOEDIT/BUFFER use the
profile-text error on rejected values; the short field commands retain their
existing extras-storage mapping if the store rejects their prevalidated input.
Only successful visibility writes request board_refresh. Only successful
BIOEDIT/BUFFER commits produce editor completion. Failed persistence cannot
publish PROFILE SAVED or a refresh/completion flag.

The existing global board guards are retained. Direct Core callers additionally
receive explicit login/revocation/store-health outcomes. No private text is
included in these outcomes or diagnostics. Administrative MEMBERPROFILE and
application data changes are not routed through this own-account API.

## Validation and limits

Targeted C11 tests with warnings as errors cover 41 Member Core groups. The three
new groups exercise owner/field forwarding, borrowed strings, the privacy matrix,
live policy/role changes, guest/stale/retired actors, invalid input, broken stores,
and rejected/failed writes without successful publication. Profile persistence
is stubbed: this test does not prove actual UTF-8/date parsing or disk durability.

Existing stage61 socket coverage is extended with all seven shorthand updates
and clears, rejected visibility writes without journal changes, public-to-private
read/search filtering with owner/admin access retained, and invalid birthdays
without journal changes. Existing long BIO/shared-buffer and restart/fault tests
remain enabled. Stage69 now checks compatibility-denied edits leave the journal
unchanged and birthday visibility still permits month/day/private transitions.
Python syntax is checked locally; execution remains in existing hosted CI and
subsequent exact-main native CI. No duplicate full local regression is run.

Reviewed canonical personal-settings and command-reference plus Japanese/English
profile HELP; their public rules remain accurate. CHANGELOG and this internal
contract document the refactor. Only reviewed source-lock entries are refreshed;
manual generation, links, search and offline parity are checked. No production
access, deployment, release or new native PR execution is performed.
