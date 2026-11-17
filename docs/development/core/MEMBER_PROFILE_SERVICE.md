# Profile service: reads, search and administrator edits

Baseline: main `a703341c79dc57dab39e96eaf9c391ed9433bbbe` (PR #88).
This LN-23/24 change groups the remaining ordinary profile service operations
into one PR: exact-name/ID/self reads, visible field snapshots, all-field and
field/group search, inclusive pagination and global-administrator edits. It
builds on the [own mutation boundary](MEMBER_PROFILE_CONTROLS.md).

## Core contracts and ownership

The trusted worker supplies MemberActor; every entry rechecks actor identity,
current epoch, suspension and member/moderation/profile/extras store health.
Core receives structured parameters, not terminal command text or a cached UI
role. These APIs require a logged-in active actor. The board adapter retains
parsing, labels, framing and error rendering for current Telnet/RAW/Web clients.

member_core_profile_get accepts either an explicit ID or an exact login name;
zero ID and NULL login mean the actor. Unknown, pending, suspended or retired
targets remain unavailable, including to administrators. Name lookup uses the
existing indexed members_find. A shared-board remote author is not converted to
a local account unless its existing exact login lookup resolves it.

MemberProfile owns only the visible fields: handle (80 bytes), biography (8192),
and eleven other fields (320 each), plus ID/name. It is under 12 KiB and does not
allocate storage proportional to account count. Values are copied before the
profile store's rotating text cache is reused. member_profile_value returns a
borrowed pointer into this caller-owned snapshot. No pointer is retained by Core.
Inputs and outputs must not overlap; callers discard snapshots after use and
reauthorize every subsequent request rather than treating a snapshot as a grant.

The existing profile_visible policy remains authoritative for owner/admin reads,
private fields and birthday month/day projection. Hidden values are absent from
the output. Invalid metadata, an overlong stored value or a detected partial-read
failure clears the entire result, including names, fields and identifiers.
Store-level fatal SQLite errors retain the existing fail-closed process behavior;
this PR does not replace that recovery mechanism.

## Search, visibility and paging

MemberProfileQuery carries a typed ALL/FIELD/GROUP scope, zero-based field or
group number, inclusive starting ID, limit (1..8), and borrowed query text. Text
retains the existing 1..80-byte/control-character rule. Zero start means 1.
MemberProfilePage owns at most eight ID/name/visible-handle rows; it contains no
private matching excerpts or total match count. next is the next unscanned ID,
not a promise that another match exists. Even limit=1 preserves that convention.

Eligibility, visibility, compatibility policy and SIGOP status are evaluated
again on every page. The adapter preserves the existing session search filter
and maps its protocol scope into the typed Core request. Changing another field
between requests cannot make it match a retained field-specific search.

Without compatibility policy, ordinary member searches use visible fields.
With policy enabled, group searches require a global admin or any current board
SIGOP; real name/kana/address/phone field searches require a global admin. SIGOP
inheritance stays in catalog_sigop. These restrictions do not grant access to
private text. Birthday searches use the same projected month/day as display.

Existing wildcard behavior is preserved: `*` enumerates eligible accounts in a
field query even when their field is empty/private. That reveals no field value
and is not a presence test. General searches may match the public login name.
Group searches test membership without returning profile/private data beyond
the same visible handle. A metadata count above MEMBER_MAX fails before scanning.

## Administrator edits and adapter behavior

member_core_profile_admin_set permits global administrators to edit fields
0..12 of active targets, including self, protected, pending and suspended
accounts. It excludes retired/missing targets and the shared buffer. This is the
existing MEMBERPROFILE policy, not an expansion of profile-read eligibility.
The store retains field/date/UTF-8 validation, actor attribution and durable
multipart writes. The API never edits membership, credential epochs or visibility.

Rejected values/targets/roles return a profile edit error. A failed append has an
explicit storage outcome internally; the board adapter keeps MEMBERPROFILE's
existing combined permission/value error reply. Existing global broken-store
checks remain in front of the adapter. No failed edit returns PROFILE SAVED.

Ordinary profile output retains seven base field lines, omission of empty newer
fields, PROFILEBODY framing for multiline biographies and END PROFILE. Search
keeps PROFILES/END NEXT and the existing one-result detail representation.
Reading an article author's profile still requires the existing article/board
visibility check before Core name resolution. Core does not accept a caller flag
that bypasses an article ACL.

## Scope left for later work

Application review/registration details, common editor-buffer loading, article
ACL extraction and the administrative application-specific full-profile view
retain their existing paths. This PR does not claim every profile_store caller
is replaced or that the complete Core/UI separation is finished. There is no
new Web transport, protocol/schema migration, production deployment or release.

## Verification

Local targeted C11 tests with warnings as errors pass 47 Member Core groups.
Six new groups cover cache-independent snapshots, 8192-byte biographies,
exact-name resolution, owner/admin visibility, pending/suspended/retired targets,
8/1-result paging, name/field/group searches, live policy and SIGOP changes,
month/day filtering, bounds and whole-output clearing after read failures.
Administrative tests cover all 13 fields, eligible account states, self/protected
targets, non-admin refusal, buffer exclusion, validation/storage failures,
unchanged visibility and actor attribution. Store/catalog behavior is stubbed;
these checks do not replace native persistence or transport tests.

The new test_profile_service.py is wired into the existing make check suite.
It uses twelve independent fixture accounts, actual sockets and profile journals
to test privacy, retained field pagination, name lookup, admin edits/refusals,
group search, long biography framing, suspension and restart persistence. Python
syntax passes locally; runtime execution awaits hosted CI and then exact-main
native CI. Existing stage61/stage64/stage69, extended menu, WebUI and restoration
regressions remain enabled; no duplicate full local run is started.

Canonical personal-settings, command-reference, permissions, member-admin and
Japanese/English profile/adminmembers HELP are reviewed against this change.
Public behavior is unchanged; the canonical manual remains on released 0.74.1.
CHANGELOG and this development contract describe the internal refactor. Only
reviewed source-lock hashes are refreshed; generation, internal/legacy links,
search and Markdown/HTML/offline parity are validated before PR publication.
