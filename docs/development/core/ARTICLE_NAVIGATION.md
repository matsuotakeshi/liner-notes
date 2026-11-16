# Related-article navigation

Baseline: main `1a22f945eac705329641a70faa4b09254a4669d7` (PR #67).
This LN-23/24 slice extracts PARENT/CHILD/NEXT/PREV selection, without changing
commands, persistent records, transport or UI profiles.

## API and ownership

`article_navigate(actor, board, current, direction, out)` returns an owned
ArticleSummary. Its ID is zero when no visible neighbour exists. Inputs are
borrowed for the synchronous call and must not alias the output. Core retains
no session selection, body, read mark or audit state. Calls remain serialized
by the existing board worker; this is not a concurrent repository API.

Core checks store health, member epoch/suspension and board read/password access
on every call. The current article must belong to that board, but may have
become hidden since selection. This preserves navigation out of a hidden
selection. Management authority does not bypass effective visibility.

PARENT follows parent pointers, CHILD starts at the first child and then follows
next-sibling pointers, NEXT follows next-sibling pointers, and PREV follows
previous-sibling pointers. Hidden targets are skipped in the same direction.
CHILD does not descend into the hidden child's subtree. Visibility includes
ancestor withdrawal and local hiding; no hidden title or body is returned.

Internal ArticleMetadata now copies child/next/prev from the existing index.
It is not a persistent record or schema change. Core rejects missing/out-of-range
targets, cross-board references and child/sibling parent mismatches. A traversal
budget bounded by article count rejects cycles among hidden targets, and a
return to the current article fails immediately. Errors clear all output bytes;
the per-call password context is cleared before return.

## Legacy adapter

board.c retains parsing, no-current/no-neighbour and other protocol responses.
Successful selection uses the existing display_article/article_read path, which
rechecks authorization, renders the body and records access as before. Selection
itself reads no body and performs no write. Session selection changes only on
successful display; an exhausted neighbour search keeps the current article.
Worker serialization prevents intervening mutations between selection and read.

## Verification and documentation

Local UCRT GCC C11 compilation with -Wall -Wextra -Werror and all ten
test_article_core groups pass. Navigation cases cover all directions, guests,
owned results, hidden current/ancestors/siblings, deep chains, empty neighbours,
revoked identity/password grants, store failures, malformed references/cycles
and absence of body reads/read marks/publications.

test_visibility.py adds runtime regressions for leaving hidden selections and
retaining selection after a failed move. Python syntax is checked locally;
actual socket regressions, full runtime and OS compatibility remain with the
existing PR/main CI, not a claimed local Windows runtime result.

Reviewed docs/manual/boards.md and Japanese/English read HELP. User commands,
navigation and explicit read-mark semantics remain unchanged, so public text is
retained. CHANGELOG and this internal contract document the extraction. Only
the four reviewed implementation source/header digests are refreshed in the
canonical manual source-lock; generation/link/search/offline checks stay enabled.
