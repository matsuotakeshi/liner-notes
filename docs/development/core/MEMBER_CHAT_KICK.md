# Member chat removal Core boundary

Baseline: main `2a165fdb9a64eb5c757ef92907b680195c13a167` (PR #83).
This LN-23/24 slice extracts the authority, target lookup and durable audit order
for /KICK. It does not extract chat messaging, personal mute controls, login or
runtime socket handling. No wire command, schema, journal or package change.

## Authority and target resolution

member_core_chat_kick takes the current MemberActor, a borrowed login name and a
caller-owned MemberChatKick output. It verifies healthy member/moderation stores,
identity, suspension and credential epoch, then requires global administrator
permission before any target lookup. It also checks extras-store health because
the combined audit capacity includes extras history. Core performs these checks
without relying on an adapter's identity or UI gate.

Login matching remains exact and case-sensitive, using the existing ascending
member-name scan. This slice deliberately does not introduce a second index or
change lookup semantics. Names must contain 1..32 bytes; empty/long/unmatched
names return NOT_FOUND, a null input returns INVALID. Names of retired accounts
remain eligible where the existing store exposes them; this neither activates
accounts nor guarantees any live chat connection. Self and protected targets
are rejected. Invalid total-member bounds and lookup failures return storage errors.

## Commit and connection responsibility

Core checks moderation_count + extras_audit_count using wide arithmetic before
calling the existing moderation_chat_kick durable writer. Repeated removal orders
are independently audited, including when there are no target chat connections.
Only successful persistence returns a copied target ID/name. Every failure clears
all output and therefore produces no removal order. Credential epochs, suspension,
preferences, member IDs and login eligibility are unchanged.

Core retains no strings, Session or socket. Inputs and output must not overlap;
the caller keeps inputs alive throughout the call. Execution remains on the
serialized board worker, with no new locking or asynchronous callback.

board.c maps Core errors to the existing chat response (including the special
ERR member not found spelling) and forwards a successful target ID to runtime.
Existing runtime code applies committed orders even if the requesting administrator
has disconnected. Only active, non-closing connections of that member which are
chatting or awaiting chat are closed. Other BBS connections remain open. Runtime
still clears input, resolves private-talk peers and reports the actual count.
The operation is not an account suspension or the general DISCONNECT command.

## Validation and limits

Local UCRT GCC, C11, -Wall -Wextra -Werror passes all 32 Member Core test groups.
Three added groups cover exact/copied names, repeat orders, target account states,
revoked/non-admin actors, self/protected targets, input and count boundaries,
combined-audit overflow, failed lookup/writes and cleared results. Persistence is
mocked; this is not proof of real fsync or socket closure.

The existing test_chat_kick.py additionally checks that removing two chat
connections appends exactly one 32-byte moderation record. Its real runtime tests
already cover unaffected BBS peers, reconnect/preferences, private-talk cleanup,
filtered durable audit, partial-write denial and Japanese/English Telnet. Python
syntax passes locally; runtime tests remain in make check and existing hosted CI,
followed by trusted-main native CI. Full regression is not duplicated locally.

Canonical conversation and Web user instructions, plus Japanese/English admin
connection HELP, were reviewed against runtime and persistence code. User behavior
is unchanged; the stable manual/version is not advanced. CHANGELOG and reviewed
source digests are updated. Canonical generation/link/search/offline validation
runs separately. No production access, service changes, deployment or release.
