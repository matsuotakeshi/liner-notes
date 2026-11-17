# Combined audit history Core boundary

Baseline: main `3cb92042437b787a451185daaa9a2eb4bd6bef95` (PR #73).
This LN-23/24 slice extracts AUDIT retrieval, filtering and authorization from
board.c. History formats, global sequence numbers, timestamps and public command
syntax are unchanged. The adapter keeps date/time-zone formatting and UI state.

## API and authorization

`article_audit(actor, current_board, start, article, actor_id, actor_name, out)`
returns an owned ArticleAuditPage. Core checks store health, current identity and
credential epoch, then management authority on the current board before resolving
filters or reading history. This replaces the old board adapter's AUDIT gate;
it does not remove that gate's semantics or move authorization into the UI.

An administrator can inspect system events. A SIGOP can inspect only article
events in their effective assigned boards, using catalog_sigop (including the
existing inherited roles). Every article event also requires current read ACL,
even for administrators. Hidden/withdrawn article events remain auditable;
ordinary article visibility is intentionally not applied to management history.
No body, historical title text or credential is returned.

Article and operator filters combine. Operator ID and exact login-name are
alternative inputs, never an author filter. Name lookup uses the existing
indexed members_find instead of scanning all 999,999 possible slots. The parser
already rejects simultaneous ACTOR/ACTORID; direct Core callers also get an
invalid-filter result for that ambiguity. Unknown actor filters are rejected.

The existing extras/moderation resolver supplies merged sequence order. Extras
article events resolve their current board through the article repository;
moderation's existing reader resolves moved article history. start=0 selects the
latest committed sequence. Up to 8 rows are returned after at most 256 scanned
entries, newest first. next is the next inclusive sequence, and can be nonzero
for an empty filtered page. Each continuation rechecks current authority.

The page owns action enums, operator names, IDs and timestamps. A read error
after partially filling a page clears the entire page. Combined-count overflow,
invalid moderation references, unknown returned event kinds and invalid operator
metadata fail closed. No history append, article read, read mark or session
mutation occurs. Calls are serialized by the existing worker; no pointers are
retained and input/output storage must not overlap. This is not a network API.

## Adapter and compatibility

board.c still emits AUDIT / EVENT / END NEXT, the same action labels and local
date rendering, and the existing filter/cursor error messages. Session filters,
AUDIT REFRESH/ALL, '+' pagination and WebUI rendering are unchanged. No new
management privilege, event type, persistence schema or retention policy is added.

## Validation

The direct Core suite now has 15 groups. Added scenarios cover merged ordering,
all action types, name/ID/article filtering, private-board suppression, SIGOP
scope and its revocation, hidden-article audit retention, guest/unknown/revoked
identity rejection, read ACL changes, 8-row/256-scan pagination, sparse pages,
storage failures after partial results, invalid references and count overflow.

One initial test incorrectly expected a SIGOP's own board password to block
their audit access. Existing policy_locked explicitly exempts board managers.
The test was corrected to exercise administrator read-ACL changes instead;
production policy and test thresholds were not changed. The single correction
and rerun passed all 15 groups with local UCRT GCC -Wall -Wextra -Werror.

Existing test_audit.py and test_audit_filters.py remain in make check, covering
actual journal ordering, restart, framing, filters, UI paging, large filtered
history and corrupt/partial writes. test_stage59.py and later regressions cover
SIGOP behavior. Those runtime tests are delegated to hosted PR CI; mocked-store
results do not constitute native OS evidence. Native CI stays trusted-main only.

Reviewed docs/manual/logs.md and command-reference.md plus Japanese/English
admin HELP. Existing user instructions remain accurate. CHANGELOG records the
extraction and source-lock updates only reviewed implementation digests. The
canonical public book and offline bundle retain their existing feature-based
structure. No production changes or release publication.
