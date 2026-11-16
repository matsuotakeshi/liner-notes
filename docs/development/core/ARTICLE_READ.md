# Structured article reads

Baseline: main `30ed7e98e13a6a0087cdb95455cb298dbfbe22df`, following PR #57.
This is a second, bounded LN-23/24 slice. It does not complete structured search,
multi-UI selection, Web's structured API, Door authentication or SSH.

## Core operation and authority

`article_read(actor, article_id, inspect, out, body, capacity)` returns a typed
outcome and an `ArticleView`. The actor comes from the authenticated worker
request, not client JSON. NULL actor or member ID 0 is a guest. Core rechecks
storage health, current member epoch/suspension, the article's actual board read
ACL and password grant, then management permission if inspection was requested.
Inspection is a requested operation, not an assertion of admin status. It never
bypasses a board's read ACL. Parent SIGOP authority remains in the existing
catalog implementation.

The six legacy display call sites now use this operation: direct READ, neighbour
navigation, NEW/unread selection, SCAN, READGLOBAL and INSPECT. Their existing
board/scope/cursor filters stay in the worker. Only a body read already selected
by those operations is extracted here. Current-board LIST/SEARCH pages are now
handled by [Article pages](ARTICLE_PAGES.md); thread/unread/cross-board selection
remains in the old worker. No new wire command exists.

## Output and non-disclosure

| Result | Structured fields | Reads article body? |
|---|---|---|
| Visible | ID, board, parent, copied author/title, body length, caller-owned body | Yes |
| Concealed by self or ancestor | ID, board, parent and concealed state only | No |
| Withdrawn article | ID, board, parent and withdrawn state only | No, even for inspection |
| Authorized inspection of a hidden article | Visible payload plus inspected-hidden flag | Yes |
| Invalid identity, denied ACL/inspection, missing article, storage/capacity error | Zeroed result and supplied body buffer | No, except a failing body read which is then cleared |

The existing memoized ancestor visibility implementation is reused, including
the distinction between a withdrawn article and a retained descendant hidden by
its parent. An unreadable or missing article reveals no metadata. Management
permission alone does not make an ordinary READ reveal hidden content.
The old ARTICLE/HIDDEN/WITHDRAWN text is formatted only in `board.c`.

## Lifetime and memory

As with mutations, calls are synchronous on the one board worker, neither
thread-safe nor reentrant. Metadata is copied from the SQLite-backed index.
No transient index pointer or borrowed body pointer is returned. `out` and the
body buffer must be valid, non-overlapping caller-owned storage. Capacity includes
the NUL terminator; insufficient capacity fails instead of truncating. A NULL
body buffer is valid only for results that do not need a body. A NULL result
pointer is rejected. No output allocation or body-sized stack object is added.

The legacy adapter temporarily uses `BoardResult.draft_body`, already allocated
with each queued result, while keeping draft flags unset. It formats the response
and cleanses this scratch buffer before delivery. Thus a read cannot overwrite a
session draft/common buffer, nor leave a second body in the queued result.
Core itself holds no retained body; a later read cannot invalidate a previous
caller's copy. Board password context is cleared when a valid-output call returns.

Selection, sequential cursors, access logging and explicit SEEN remain the
worker/session's responsibilities. Core reads do not mutate them. The existing
worker storage-failure override remains active. This API still relies on the
worker's overall lifecycle/feature checks; it is not an independent service.

## Verification

`make test-article-core` now tests guest reads, inspector denial, read ACL denial
even for a manager, suspended/stale/unknown identities, revoked password grants,
ordinary hidden reads, privileged hidden reads, withdrawn tombstones, output
ownership, exact buffer capacity, partial body-read failure, visibility failure
and unhealthy stores. Denial/tombstone assertions check that author/title/body
do not escape and that the repository body function was not called. Existing
withdrawal/retitle tests still run in the same target.

Local UCRT GCC unit compilation/execution passed with `-Wall -Wextra -Werror`.
Full runtime validation uses the existing hosted Linux PR CI; the previously
identified native-Windows POSIX dependency is not retested or altered here.
Existing `test_board.py`, `test_members.py`, `test_extended.py`,
`test_moderation.py`, `test_large_content.py`, shared-board/Web tests and stage63
cover legacy text, cursors, read-state, access controls, large bodies and RSS in
the full runtime. Results must refer to this PR/main SHA, not PR #57's baseline.

The current public manual/HELP behavior is unchanged. The reviewed descriptions
are `docs/manual/boards.md`, `article-search.md`, `board-admin.md`, `links.md`,
`logs.md` and Japanese/English CONTENT/READ HELP. Implementation source hashes
are updated for this extraction; development-only APIs stay out of public HELP.
