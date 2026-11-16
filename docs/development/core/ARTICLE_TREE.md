# Structured article trees

Baseline: main `3d29839d4f3f4acf78586763446e1e4530733b15` (PR #60).
This LN-23/24 slice extracts TREE into the same worker-owned article Core as
reads, lists and search. NOTES/unread/cross-board selection and UI profiles remain
unfinished. No new public command or transport is introduced.

## Service, authority and ownership

`article_tree(actor, board, article, start, out)` returns `ArticleOutcome` and a
caller-owned `ArticleTreePage`: thread root, next cursor and up to eight copied
rows (article ID, board, parent, title and structural depth). No article body is
read and no index pointer is retained. Inputs must not alias the output. The
existing single worker owns all calls; this is not a thread-safe standalone API.

Every call checks storage health, current member epoch/suspension and board
read/password access. A guest may view a readable board; a manager cannot bypass
read access or visibility. The target must belong to the requested board. Core
derives its root from repository metadata, never from a caller's authority claim.
Each cursor and traversed node must belong to that same board and root. A stale
cursor is not an authorization lease. Password context is cleared after calls.

Metadata now copies root/depth from the existing index. This changes only an
internal C value structure, not the persistent Article layout or SQLite schema.
The repository wraps the existing depth-first `tree_next` traversal. No second
tree index, recursion, retained body, or full-thread allocation is introduced.

## Preserved behavior

Selecting a reply still displays the whole containing thread, starting at its
root when start=0. Nonzero cursors resume at that node in existing depth-first
order, which need not be ascending by ID. Invalid/mismatched cursors return the
existing `ERR tree cursor` response. Eight visible rows form a page; hidden nodes
are skipped before choosing the next visible cursor. A hidden tail produces
next=0, not an unnecessary empty continuation page.

The common effective visibility check excludes both hidden/withdrawn articles
and their descendants. An entirely concealed thread retains only its existing
root identifier in the empty TREE header; no title/body or hidden row is returned.
IDs, parents and depths are never renumbered. RESTORE does not override ancestry.

Depth remains structural data. Only the legacy adapter applies two spaces per
level, capped at 24 spaces, and formats the existing TREE/row/END NEXT text.
Current selection, explicit SEEN, drafts, read logging and link synchronization
remain unchanged. Page creation has no storage write side effects.

Any visibility or metadata failure during collection clears the whole result,
including previously copied titles, root and cursor. Unexpected cross-thread or
out-of-range traversal also fails closed. Repeated visible nodes within a page
are rejected, and a count-based visit bound prevents endlessly following a
repository-returned loop of hidden nodes. These are service-boundary guards,
not a replacement for the repository's existing validated-tree invariants or a
claim to repair arbitrary corrupt index graphs.

## Verification and documentation

Direct Core tests cover branching DFS with decreasing IDs, reply-to-root
selection, copied titles/parents/depths, 8-row pagination, hidden next/tail/all
pages, changed visibility, wrong-board/wrong-root/out-of-range cursors, missing
targets, identity/read/password revocation, unhealthy stores, partial-result
clearing, invalid traversal and bounded hidden loops. Body probes remain unused.
Existing read/mutation/moderation/list/search unit tests remain in the target.

Local UCRT GCC direct tests passed with `-std=c11 -Wall -Wextra -Werror`.
`tests/test_notes.py` gains a real-protocol assertion that TREE on a reply
returns its containing root and the same rows. Python syntax checking passed;
actual runtime execution remains with existing hosted `make check`. That suite
already covers branching order, concurrent replies, restart and deep trees.
Shared visibility/Web regression remains enabled. Full Windows runtime is not
claimed; native OS tests execute only trusted main code after integration.

Public manual `boards.md`/`board-admin.md` and Japanese/English READ/CONTENT HELP
were reviewed: their behavior is unchanged and their wording is retained.
Technical descriptions and reviewed source-lock hashes are updated here. Tests
of this change must cite its own PR/main SHA, not the successful PR #60 baseline.
