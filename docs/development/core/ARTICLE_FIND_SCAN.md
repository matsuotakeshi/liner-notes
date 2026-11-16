# Structured cross-board filters and sequential selection

Baseline: main `1c965b5ef9f7a6ae261f0d26cbc88afe3a6e164b` (PR #66).
This LN-23/24 slice extracts FIND/FINDFROM and SCAN candidate selection. It does
not introduce a new command, schema, network API or complete the multi-UI work.

## API and ownership

`ArticleFilter` borrows board, HERE/ALL/SUBTREE scope, author and query for one
call. Author is an existing exact login name or `*`; query is a case-sensitive
literal 1..80-byte string without controls, or `*` for no term filter. Imported
or anonymous authors remain discoverable via the wildcard. Named-author lookup
retains the existing members_find rule, not a new foreign-origin identity map.

`article_find(actor, filter, start, out)` returns an owned ArticlePage with up to
eight ID/board/parent/title rows after at most 256 index candidates. `total=0`
and `next` is the next unscanned ID; an empty continuation page is valid.
Candidate jumps through the existing FTS index count as one candidate each.

`article_scan(actor, filter, start, descending, out)` returns an owned ArticleScan:
one matching ArticleSummary and the immediately adjacent cursor in that
direction, or article.id=0 for end. SCAN retains its unbounded-per-request search
through the finite index; it does not stop after 256 rejected candidates.
Descending starts beyond the end clamp to the article count. UINT32_MAX thus
retains the existing newest-article sentinel. In both APIs, start=0 means 1;
forward cursors through CONTENT_ID_MAX+1 may return an empty end result.

Core owns no session cursor or search state. Output must not alias inputs.
All operations run on the existing single-owner board worker, synchronously.
The APIs are not concurrent storage or a client authentication mechanism.

## Authorization, matching and failure handling

Core checks current member/store health, epoch/suspension, scope and result-board
read/password permissions on every request. HERE requires read access to its
board; ALL/SUBTREE may select readable descendants of an unreadable parent.
Management authority never bypasses result ACLs or effective visibility.

The index is only a candidate selector. Core checks scope/ACL, effective ancestor
visibility and exact author before title/body matching. A matching title or `*`
needs no body read. Only authorized visible candidates reach body_contains.
No body or inaccessible title/count escapes in the structured result.

Bad candidate bounds, backward progress in ascending scans, forward progress in
descending scans, metadata/visibility/body failure and impossible store counts
fail closed. Errors clear the whole output, including collected rows/cursors.
The count limit and strictly advancing cursor bound traversal without accepting
an infinite index cycle. Existing SQLite fail-closed behavior is unchanged.
Password context is cleared at return; no mark, audit or journal is written.

## Legacy adapter and session responsibilities

board.c retains command parsing results, FIND scope/author/query header and
four-column row formatting, END NEXT, SCAN END and existing error strings.
SCAN uses its selected ID with the existing display_article/article_read path,
which rechecks the actual board's read authority before returning a body. Board
worker serialization prevents intervening mutations between selection and read.
Session keeps CONTINUE, P, relative movement, STOP, saved search conditions and
the selected board. Existing display/read auditing stays at that boundary.

No end cursor is reused as authorization and no new global body buffer or
full-result array is allocated. This slice does not redesign full-scan complexity
or claim completion of authentication, transport, UI profiles or DB normalization.

## Verification and public documentation

UCRT GCC C11 with -Wall -Wextra -Werror passes all nine test_article_core groups.
New direct tests cover scopes/private boards/guests, exact author and literal
matching, wildcard/title body-read avoidance, output ownership, ascending and
descending selection, sentinel/empty/end cursors, 256-candidate continuation,
index jumps/bad direction/out-of-bounds, revoked identity/password grants,
health/lookup failures and erasure after partially collected rows.

test_extended.py adds descending SCAN/CONTINUE through SCAN END, invalid authors
and guest subtree filtering. Python syntax is checked locally; real runtime,
WebUI, shared visibility, persistence and package checks use existing hosted PR
CI. Native CI runs only after trusted-main integration. No local full Windows
runtime or production deployment is claimed.

Reviewed docs/manual/article-search.md and both languages of HELP read/unread.
Existing user-visible commands, conditions, date behavior and read marks remain
unchanged, so public wording is retained. CHANGELOG and this internal contract
record the refactor, and the four changed implementation sources/headers are
reviewed in source-lock. Canonical generation/link/search/offline parity checks
remain enabled without weakening the source-lock guard.
