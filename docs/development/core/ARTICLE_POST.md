# Local article editing admission and posting

Baseline: main `364739cf195028b33d8d7fe257bc3f4db2ce8a61` (PR #70).
This LN-23/24 slice extracts local POST/REPLY admission and commit into Core.
Session editors, saved-draft persistence/resume, protocol rendering and shared
board import authentication remain in their existing layers.

## API and trust boundary

`article_prepare_post(actor, board, parent)` checks store/identity health, current
board read access, guest-post policy, parent board/effective visibility, write
access and legacy multi-board compatibility. It starts no editor, reads no body,
reserves no ID and writes nothing. A successful result is not a capability.

`article_post(actor, board, parent, guest_name, title, body, out_id)` repeats those
checks and then validates live storage availability, member/legacy compatibility,
body limit, cumulative global/member limits, per-board capacity, title/body size
and legacy record capacity. Core also rejects control characters in titles and
author labels, matching existing terminal input validation. Body text is borrowed
unchanged, including line endings; there is no new body normalization.

The authenticated member name always wins over guest_name. Guests retain the
existing supplied handle or fallback `guest`, only when current policy allows
posting. No caller-supplied author ID, administrator flag, creation timestamp,
remote origin, quota count or saved authorization result is accepted. Actor
identity/epoch/grants come from the trusted session, not a network JSON object.

All strings are borrowed, NUL-terminated and valid for the synchronous call.
out_id must not alias input storage. No pointer is retained, no full-body copy
is added to Core, and every error leaves out_id=0. The existing board worker
serializes permission checks, quota snapshots and append. These APIs must not
be invoked concurrently outside that owner.

## Storage and legacy adapter

ArticlePostingPolicy is a store-owned snapshot of version, guest/member policy,
storage failure, configured limits and cumulative author count. Zero global or
member limits retain their existing unlimited meaning. Per-board quota counting
continues through board_usage; this change does not optimize its scan or change
which withdrawn records contribute to capacity.

The private append_article_record helper retains record sizes, compact-body
encoding, checksum, offsets, timestamp rules, author/board fields and fsync
ordering. ID, index and local author counter publication follow successful
journal write/sync. An uncertain append sets the existing failed flag and does
not acknowledge an ID; later local writes return storage unavailable. Existing
index-layer failures still follow that layer's fail-closed behavior. This is not
a new transaction spanning journal/index/link databases or a rollback mechanism.

Core's repository append accepts only local inputs. Imported posts continue via
the existing link validation/checkpoint path, which calls the private encoder
with its verified alias/timestamp. Their counters and hidden-parent replication
rules remain unchanged; local reply visibility rules are not applied to imports.

board.c translates outcomes to the existing EDIT/SAVED/ERR responses. Session
selection, explicit SEEN, editor state and saved-draft deletion are unchanged.
Editing may begin while a quota is full or a legacy body size is insufficient;
the final post still checks those conditions, as before.

## Validation and remaining work

UCRT GCC C11 -Wall -Wextra -Werror passes all twelve article Core test groups.
Posting cases cover roots/replies, caller-name spoof refusal for authenticated
members, guest handles, disabled membership, revoked epochs/grants, hidden or
foreign-board parents, quotas/zero limits/ID exhaustion, exact 80-byte title and
256KiB body boundaries, controls, legacy versions 1..4, store errors and no
acknowledged ID after append failure. Editing admission is followed by changed
policy in tests to verify commit-time checks.

test_limits.py adds a real-socket concurrent-session quota regression: begin a
draft, consume the quota from another session, reject the first commit, compare
stored bytes, restart and confirm the next ID remains available. Python syntax
passes locally. Existing post/reply, short-write, legacy/restart, saved draft,
moderation, WebUI and signed-link regressions remain in the hosted PR suite.

An additional Windows board.c syntax check caught newly added enum outcomes in
an exhaustive adapter switch; these were explicitly handled. Its remaining
errors (struct flock declaration and lstat) are also present in the unchanged
baseline. Local logs are build/board-syntax-base.log and
build/board-syntax-current.log. Full Windows runtime compilation is not claimed;
no platform compatibility workaround or warning suppression was added. Native
OS validation waits for trusted-main integration, with no production changes.

Reviewed docs/manual/editor.md and Japanese/English post HELP. Public operations,
limits and saved-draft behavior remain unchanged, so those texts are retained.
CHANGELOG and this contract record the extraction. Only the four reviewed
implementation digests are updated in manual source-lock. Canonical manual,
link/search and offline parity checks remain enabled.
