# Member chat settings Core boundary

Baseline: main `1d1b220b347996127f7d7d645ae32b0755e66b29` (PR #84).
This LN-23/24 slice separates personal chat receive/mute controls from their text
commands. Chat membership, message delivery, kick, login and sockets remain in
their existing paths. No journal/schema, public command or package change.

## Structured operation and authority

ChatPreferenceAction describes QUERY, RECEIVE_ON, RECEIVE_OFF, MUTE and UNMUTE.
board.c parses existing /MUTES, /TALK ON|OFF and /MUTE|/UNMUTE commands and retains
their original text for the downstream UI. Core and persistence do not parse
terminal commands. Unknown text retains the existing chat-setting error path.

member_core_chat_preferences takes a current MemberActor, an action, optional
borrowed target login and caller-owned ChatPreferences output. Core rechecks store
health, identity, suspension and credential epoch. The setting owner is always
the actor; the login argument identifies whom that owner wants to mute, never
an account whose settings the caller can edit. Receive/query require NULL login;
mute/unmute require an exact 1..32-byte name. Invalid actions and arguments are
rejected before accessing settings. Inputs/output must not overlap.

QUERY performs no update. Other actions call members_chat_update, the structured
replacement for the internal members_chat_control API. The store still resolves
registered names, rejects self/unknown targets, selects the existing 32 slots,
suppresses duplicate operations and enforces revision saturation. Its 1/0/-1
results mean saved-or-no-change / invalid-target-or-capacity / storage-failure.
The C/M journal record encoding, append-before-publication and incremental update
algorithm are unchanged. Old saved settings need no migration.

## Snapshots and session responsibility

Core returns a complete owned ChatPreferences snapshot (revision, deny and 32
mute-name slots). Error paths clear the entire output, including partial reads.
No credential epoch is advanced. A successful write followed by readback failure
may leave the setting committed; an error does not promise rollback.

The existing serialized worker owns persistence calls. Core retains no pointer,
Session or socket and introduces no new locking. board.c publishes chat_user only
after a successful snapshot. Existing runtime/chat code applies revisions across
the member's connections and handles visibility and private reception. Ordinary
chat operations also obtain their existing preference snapshot through the read
API. Login initialization keeps its existing direct getter.

## Validation and limitations

Local UCRT GCC C11 with -Wall -Wextra -Werror passes all 35 Member Core groups.
Three new groups verify default/full 32-slot snapshots, owned copies, per-user
isolation, every structured action/target, stale/guest/retired/pending actors,
invalid enum/name contracts and update/read failures. Persistence is mocked: these
checks establish Core contracts, not actual slot allocation or fsync behavior.

Added real-runtime assertions to test_chat_preferences.py: /MUTES and a rejected
unknown mute target do not grow the journal. Python syntax passes. Existing
make check tests cover durable C/M records, duplicates, incremental multi-session
updates, offline targets, reconnect, filtered history and damaged/partial writes.
They remain delegated to hosted CI and subsequent trusted-main native CI.

A targeted Windows syntax check of members.c is blocked by the existing
storage_sqlite.h struct flock declaration under UCRT -Werror. The same diagnostic
was reproduced from the unchanged baseline members.c. No unrelated compatibility
fix or compiler-flag relaxation was applied. This check is not counted as passing;
real FreeBSD/Linux compilation and runtime validation remain pending CI.

Reviewed canonical conversation/personal-settings text and Japanese/English chat
HELP. User semantics and stable manual version are unchanged. CHANGELOG and source
digests are updated, and canonical generation/link/search/offline validation is
performed separately. No production access, deployment or release.
