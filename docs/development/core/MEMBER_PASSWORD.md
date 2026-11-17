# Member password Core boundary

Baseline: main `b21d6db2de83f3488f157b74f3a88e3efa77649e` (PR #80).
This LN-23/24 slice extracts PASSWD and RESETPASS authority and completion
handling. Registration/login, cryptographic storage and interactive secret
input remain unchanged. No new command, schema or password policy.

## Authority and secret lifetime

member_core_reset_check is a read-only prompt authorization check: current
identity/epoch, healthy stores, global administrator, existing target name,
not self and not protected. member_core_reset_password repeats that same check
at commit; successfully opening a prompt is never an authorization token.
The existing name-based prompt behavior is preserved, including retired names;
the existing store rejects ineligible reset targets when attempting persistence.
Pending/suspended targets are not approved or resumed by resetting a password.

member_core_password permits only the authenticated actor's own password change
and rejects guests/revoked actors. Current-password verification and unchanged
password rejection remain in members_password. Core checks borrowed strings for
NULL and the existing 8..128-byte bounds using a bounded scan, without creating
copies or logging/retaining them. Callers supply valid NUL-terminated strings,
keep input/output storage disjoint, and own secure cleanup on every path.

The existing session code still handles non-echo input, confirmation, cancel,
throttling and cleanup. The board worker still cleanses request password fields
following execution. members.c still owns random salts, PBKDF2, durable records
and temporary cryptographic buffer cleanup. This extraction introduces no second
verifier, secret buffer, logging channel or plaintext persistence.

## Completion and connections

A store result of exactly one advances the credential epoch once and returns
only target ID/epoch. Invalid credentials/input or store errors return zeroed
change output. Resetting to the same supplied password remains a durable reset
with a new salt/epoch, while an unchanged own password remains rejected.

board.c forwards the committed change using existing runtime notifications.
Only own-password success sets password_changed: runtime updates the initiating
connection to the new epoch before revoking the account's other connections.
Admin reset does not set that flag and revokes the target's old connections.
Core itself retains no Session or socket and never closes a connection.
Prompt text and PASSWORD CHANGED / PASSWORD RESET / error responses stay in
the adapter; UI semantics and Japanese/English translations remain unchanged.

## Validation and limitations

Local C11 UCRT GCC tests with warnings as errors pass all 23 Member Core groups.
One test-local variable/function name collision was corrected before the passing
run. New tests cover borrowed inputs, null/length boundaries, wrong/unchanged
passwords, old epochs, prompt-to-commit authority changes, self/protected targets,
retired/pending/suspended reset behavior and failed persistence. Persistence and
cryptography are mocked; these are not cryptographic or real fsync proofs.

test_password.py now verifies a wrong-current attempt leaves journal size
unchanged. test_stage59.py similarly checks reset mismatch/cancel and continued
use of the target connection. Existing tests cover real salted storage, peer
revocation, owner survival, restart, hidden input and failed writes. Syntax is
checked locally; real runtime tests remain in existing hosted CI and subsequent
trusted-main native CI. Full regression is not duplicated locally.

Reviewed canonical accounts/member-admin instructions and Japanese/English
account HELP. Existing public descriptions remain accurate. CHANGELOG, internal
contracts and reviewed implementation digests are updated. Canonical manual
generation, links, search and offline parity are verified separately.
No production access, deployment or release publication.

The following [Member preferences slice](MEMBER_PREFERENCES.md) extracts
actor-owned PREFS operations without changing credential epochs.
