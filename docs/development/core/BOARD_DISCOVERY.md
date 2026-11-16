# Board discovery and unread navigation

Baseline: main `4e1b5e7c557146e7c4c61613d9d0275ffca33ced` (PR #68).
This LN-23/24 slice moves BOARDS and NEXTBOARD selection/counting out of the
command adapter. It does not change the catalog format, article storage, wire
commands or UI profiles. Board selection/password entry remains in its existing
session/worker path.

## API and ownership

`article_boards(actor, start, children_only, out)` returns an owned page of up to
eight ArticleBoardRow values. Each row has ID, parent, depth, title, write
capability, unread count and a separate unread_known flag. start=0 means board 1;
next=0 means end, otherwise next is an inclusive catalog cursor. Catalog order,
including configured placement and hierarchy, is retained rather than ID order.

children_only retains the menu-node adapter's direct-child filter: its root is
the normalized start board. It is not a new independent subtree/pagination
contract. Ordinary BOARDS continuation uses next as the next start without this
filter. No recursive descent semantics or new command are introduced.

`article_next_board(actor, current, out)` requires membership and returns an
owned board row, or id=0 if no other accessible board has unread articles. It
starts after current, wraps to board 1 and stops on returning to current.
The caller retains the selected board on an empty result. Core does not select
a session board or acknowledge any article as read.

All pointers are borrowed for one synchronous call and inputs must not alias
outputs. The existing single-owner board worker serializes catalog, identity,
read-state and article access. These APIs are not a concurrent repository or
an authentication endpoint. Session identity and password grants are trusted
inputs, never caller-supplied visibility or administrator flags.

## Authorization and failure handling

Every request rechecks store health, member epoch/suspension and current catalog
ACL/password grants. Guests may list visible boards but do not read member seen
state. A visible, password-locked board can retain its title/write capability,
as before, but unread_known is false. NEXTBOARD skips unreadable boards entirely.
SIGOP write capability remains whatever catalog_write authorizes; it does not
make unread counts available without catalog_read.

Only readable, effectively visible articles contribute to unread counts. Hidden
ancestors and withdrawals are handled by the existing repository visibility
boundary. No body is loaded, no journal is written and no read mark changes.
Counts use catalog-sized scratch arrays and a metadata pass, preserving the
existing O(article count) scan rather than claiming a new indexing optimization.

Malformed article board IDs, failed metadata/visibility/seen-state reads,
out-of-range/missing catalog cursors and repeated catalog traversal IDs fail
closed. Errors erase the entire page/row, including partial results. Catalog
depth/parent consistency and order remain owned by the catalog loader; Core
also bounds returned parent/depth metadata and validates traversal results.
The per-call password context is cleared before returning.

## Adapter and validation

board.c keeps BOARDS/END NEXT, indentation, write=yes/no, unread=- versus zero,
BOARD and NEXTBOARD NONE text responses and session board changes. It no longer
enumerates articles or computes unread counts for these commands. Existing
error strings are reused for storage and revoked-access failures.

Local UCRT GCC C11 with -Wall -Wextra -Werror passes all eleven article Core
groups. New direct cases cover catalog order, eight-row continuation, child
filtering, owned titles, guest/locked/private boards, unseen counts, wrap/end,
revoked identity/grants, broken stores, bad IDs/cycles and result erasure after
partial output. Body/read-mark/write absence is asserted.

test_catalog.py adds actual command regressions for private-board exclusion,
NEXTBOARD wrap/order, guest refusal and unchanged read counts. Python syntax
passes locally; real socket execution and OS regression use existing hosted
PR CI and trusted-main native CI. No local full runtime result is claimed.

Reviewed docs/manual/boards.md, article-search.md and Japanese/English read and
unread HELP. Public operation remains unchanged, so those descriptions are
retained. CHANGELOG and this contract record the refactor; source-lock updates
only the three reviewed implementation source/header digests. Canonical manual
generation, links, search and offline parity remain required.
