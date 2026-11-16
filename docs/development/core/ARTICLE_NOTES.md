# Structured thread summaries

Baseline: main `170d0cffd4df7600c6e46563aef2d048521de619` (PR #62).
This LN-23/24 slice extracts NOTES and NOTES UNREAD. Cross-board queries and
Session UI profiles remain separate work; this is not completion of the broader
Core/UI refactor. No public API, command, configuration or schema is added.

## Contract and authority

`article_notes(actor, board, start, unread_only, out)` returns `ArticleOutcome`
and a caller-owned `ArticleNotesPage`. Up to five copied root summaries contain
ID/board/parent/title, visible reply count, greatest visible article ID, that
article's creation timestamp, and the caller's unread count including the root.
`unread_known` distinguishes authenticated counts from a guest's unavailable
read state. No article bodies or transient index pointers are returned.

Root IDs are ascending; start is inclusive, with 0 meaning 1. A reply ID used as
a cursor skips its lower-numbered root, rather than selecting that thread.
Total counts all qualifying roots, independently of start. Next is the sixth
qualifying root, or 0. NOTES UNREAD includes roots with a nonzero unread count.
Latest is determined by ID, not by comparing timestamps: link/import timestamps
may be out of order. Zero timestamps remain unknown at the formatting boundary.

Core checks storage health, live member epoch/suspension and board read/password
access on each call. Guests may list readable boards, but NOTES UNREAD requires
authentication. A manager cannot bypass read access or effective visibility.
Hidden/withdrawn parents exclude their whole thread. Hidden descendants do not
affect reply/unread counts, latest ID or timestamp. Merely listing never marks
articles read and cannot change another member's read flags.

Calls run synchronously on the existing single board worker, are not reentrant
or thread-safe, and borrow actor/grants only until return. Policy context is
cleared on return. Inputs must not alias output. Any metadata, visibility,
read-state or invalid traversal failure clears the complete output, including
previously collected rows/counts/timestamps. No partial success is published.

## Traversal and memory

The old temporary unread-count array proportional to total article count is
removed. Core scans root metadata and follows the existing depth-first index
once per visible root. Each visited node must belong to the same board/root;
non-root nodes must have a parent. A shared count-based visit budget rejects
cycles, including hidden cycles, and out-of-range traversal. Healthy work is
linear in repository metadata plus traversed nodes, with a fixed five-row
output and one temporary summary rather than a full-article allocation.

This relies on the repository's validated tree invariants. It is not a general
corrupt-graph repair or an indexed cross-board query implementation. Guest
requests now compute statistics for all visible roots during the scan, even
outside the returned page; runtime performance remains subject to regression
CI. Existing storage/index/read-state caches are unchanged.

`ArticleMetadata.created` copies the existing timestamp. Only the internal
value structure changes, not persistent Article records, SQLite tables, IDs,
timestamps, or save order. The board adapter formats the unchanged NOTES header,
row and END NEXT responses, uses the existing timezone/date pattern, and renders
guest unread as `-`. Existing early worker guards and session routing remain.

## Verification and documentation

Local UCRT GCC direct tests pass with `-std=c11 -Wall -Wextra -Werror`: pagination,
reply cursors, member isolation/filtering, guest access, copied titles, visible
counts, latest-ID versus timestamp order, zero timestamps, hidden parents and
children, revoked identity/grants, health/read failures after partial output,
invalid roots/boards/cursors, bounded visible/hidden cycles, 300 roots and a
300-node thread. The six previous Core test groups remain enabled and pass.

`test_notes.py` adds guest unread formatting and reply-ID pagination assertions;
existing cases cover concurrent replies, restart and a 10,000-level legacy tree.
`test_members.py` adds guest rejection and per-member NOTES UNREAD counts after
explicit marks. Python syntax passes locally; actual runtime execution is left
to existing hosted `make check`, including shared visibility/Web coverage.
Full Windows runtime is not claimed. Native tests run trusted main only, and
results must reference the resulting SHA rather than the successful baseline.

Reviewed public `docs/manual/boards.md`, `article-search.md` and both languages
of UNREAD HELP. Their commands/semantics are retained; public text is unchanged.
CHANGELOG, this internal contract and reviewed source-lock hashes are updated.
