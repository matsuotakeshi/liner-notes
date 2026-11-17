# Member preferences Core boundary

Baseline: main `11fc770b0f359b91026a953761be9e7a8b88c3a3` (PR #81).
This LN-23/24 slice extracts PREFS read/save/load from board.c into Member Core.
There is no command, wire-response, journal or schema change. Login restoration,
menu-mode settings and session-side application of values remain separate paths.

## Authority, ownership and lifetime

member_core_preferences and member_core_save_preferences require a current,
authenticated actor with healthy member/moderation stores. They recheck identity,
suspension and credential epoch even when called without the command adapter.
There is no target-member parameter: only the actor's own record is accessible.
No credential epoch or connection-revocation notification is changed by preferences.

MemberPreferences is a caller-owned value snapshot: present, language, pager,
editor, columns, rows and packed auxiliary settings. The latter retain existing
editor line-number and transfer-protocol semantics. Core retains no pointer,
Session, socket or mutable global state. Callers keep input and output disjoint;
save ignores input.present. A missing record succeeds with the whole output zeroed.
Failures also clear all output, including after partial reads. Legacy language/
pager-only records preserve zero dimensions rather than inventing new defaults.

These APIs run in the existing serialized board worker. They do not make the
underlying member store thread-safe or introduce a new transaction boundary.

## Validation and persistence

Language and pager accept 0 or 1; invalid/null save input returns the existing
preferences error. members_save_ui_preferences still owns editor 0..1, columns
20..240, rows 5..100 and auxiliary 0..15 validation, durable append and duplicate
suppression. Its failures retain the existing member-storage error mapping.
Neither duplicate saves nor reads append a record.

Save returns a persisted snapshot after the existing store operation. If a read
fails after a successful save, the operation returns a storage error with zeroed
output; the durable write may already exist. This is not a rollback guarantee.
No second journal, migration or alternate preference source is introduced.

board.c keeps command-shape validation and PREFS NONE/STORED/SAVED/LOADED text.
For LOAD it forwards all snapshot fields to the existing session application;
the worker skips its old second UI-preference read for this operation only.
Login's existing getter and menu_preferences lookup remain unchanged. Existing
normal logout/disconnect save requests use the same extracted operation.

## Validation and scope limits

Local C11 UCRT GCC tests with warnings as errors pass all 26 Member Core groups.
Three new groups cover missing/legacy records, full snapshots and ownership,
minimum/maximum dimensions, duplicate saves, guest/revoked/pending/suspended/
retired actors, invalid inputs, failed writes, partial reads and readback failure
following a write. They assert preference operations never revoke authentication.
These tests mock persistence; they do not establish real fsync or runtime behavior.

The existing test_preferences.py now also checks that duplicate SAVE and LOAD
leave journal size unchanged. Python syntax is checked locally. Its real socket,
restart and persistence tests run in existing hosted CI and trusted-main native
CI; full regression is not duplicated locally.

Reviewed canonical personal-settings, navigation and command-reference pages and
Japanese/English settings HELP. Public behavior remains unchanged, so no stable
manual claim/version is advanced. Internal contracts, CHANGELOG and reviewed
source digests are updated; canonical generation/link/search/offline checks are
run separately. No production access, deployment or release publication.
