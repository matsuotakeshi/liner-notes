# Structured current-board article pages

Baseline: main `3e7be1e2c6531a8564356fdcc8549a281104ad90` (PR #59).
This LN-23/24 slice extracts ordinary LIST and current-board SEARCH/SEARCHFROM.
It does not yet extract NOTES, TREE, FIND, NEWS, unread or sequential navigation,
and does not complete Issue #54 or provide a network-facing structured API.

## Service and ownership

`article_list(actor, board, start, out)` and
`article_search(actor, board, start, query, out)` return an `ArticleOutcome` and
an owned `ArticlePage`. Rows contain ID, actual board, parent and a copied title;
no body, credential or transient repository pointer escapes. The page has room
for eight rows and requires no allocation or full-body scratch buffer.
Caller output must not alias actor/grants/query input. All calls run synchronously
on the existing board worker, not concurrently or reentrantly.

The legacy adapter only formats the page into the unchanged LIST/SEARCH rows
and END NEXT line. Session/menu/Web connector behavior and command parsing stay
unchanged. The caller's cursor is a request, not an authorization capability.
Core retains no per-session query, cursor or SEEN state and does not record reads.

## Authority and failure handling

Every page checks member/storage health, current identity epoch and suspension,
then current board read ACL/password grants. Guests can list/search a readable
board. Management authority does not bypass read access or ancestor visibility.
Rows outside the requested board or concealed by self/ancestor are omitted.
The total counts visible rows only, not hidden titles or tombstone placeholders.

Core authorizes visibility before the body-contains repository operation. A
title match requires no body read; bodies are read only for authorized visible
candidates whose titles do not match. The SQLite index selects candidates but
does not grant authority. Its existing fail-closed SQLite error handling remains
unchanged. Body/visibility failure or invalid candidate progress returns a storage
error and zeros the whole page, including rows already collected and the count.
No partial page is presented as success. Password context is cleared on return.
The worker's global lifecycle/storage/feature guards still apply; this is not a
standalone service or a client-authentication boundary.

## Preserved pagination and matching

| Operation | Rows/page | Next cursor | Total |
|---|---|---|---|
| LIST | 5 | Next visible article after the page, or 0 | All visible current-board articles |
| SEARCH | 8 | Next unscanned ID, or 0 | 0; no complete-result count |

Start 0 means first page internally. The legacy SEARCHFROM parser continues to
reject an explicit zero; no wire syntax changes. LIST beyond the end still
returns an empty page with the visible board total. SEARCH permits count+1 as
an empty final page and rejects larger cursors. The existing CONTENT_ID_MAX
bound reserves space for end cursors.

SEARCH scans at most 256 candidates per request, including candidates excluded
by ACL/visibility or final literal matching. An empty page with nonzero next is
valid. Existing trigram FTS candidate jumps and short-query fallback are reused;
the final case-sensitive literal title/body match, UTF-8 bytes, 1..80-byte query
limit and rejection of control bytes are unchanged. A search index candidate
outside the requested forward range fails closed rather than looping.

LIST still performs a board-wide metadata scan to produce its total. This step
does not claim to optimize full-list counting, introduce a DB schema or remove
all O(n) work. Index/cache implementation and body residency are unchanged.

## Verification and public documentation

The existing `make test-article-core` target now also checks visible totals,
owned metadata, 5/8-row boundaries, parent IDs, empty/end/out-of-range cursors,
query limits, case sensitivity, title/body matching, 256-candidate continuation,
index jumps and invalid progression, guest access, changed visibility, revoked
identity/read/password permissions, unhealthy stores and partial-result clearing.
Body spies assert that hidden/other-board rows never trigger body inspection.
Existing mutation/read/local-moderation tests remain in the same target.

Local UCRT GCC tests passed with `-std=c11 -Wall -Wextra -Werror` after adding
the existing ID-limit header dependency. Full Windows runtime is not claimed;
its existing POSIX dependency remains outside scope. Hosted `make check` includes
real LIST/search regression, Japanese/English Telnet tests, moderation, large
data and persistence coverage; shared-board/Web CI continues separately. Native
tests run only after trusted-main integration. Attach results to the exact SHA.

Reviewed current public descriptions: `docs/manual/boards.md`,
`article-search.md`, `board-admin.md`, and Japanese/English search/content HELP.
Commands, matching, pagination and visibility are unchanged, so public wording
is retained. Source-lock hashes are refreshed after review. Development APIs
remain documented here rather than being advertised as finished multi-UI APIs.
