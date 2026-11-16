# Core boundary: local article moderation

Baseline: main `c6da88503c7f56bf5651a495062acb1b11896add` (PR #58).
This bounded LN-23/24 step extracts HIDE/RESTORE into the existing article Core.
It does not complete the UI adapters, list/search APIs or the wider refactor.
There is no new user command, setting, storage format or network protocol.

## API and trust boundary

`article_hide(actor, board, article)` and `article_restore(actor, board, article)`
return `ArticleOutcome`, not presentation text. They have the same worker-only,
borrowed-actor, synchronous lifetime as [article operations](ARTICLE_OPERATIONS.md).
They must run on the existing board worker; the caller cannot supply an owner,
manager or visibility flag. They are not standalone RPC or authentication APIs.

Each call checks storage health, member existence, suspension/epoch, current
`catalog_manage` authority and `catalog_read` permission for the actual board.
The requested board must match the article. The existing catalog resolves
inherited SIGOP roles; authorship alone grants no HIDE/RESTORE authority. Board
password grants are borrowed for this call and cleared on every return, so a
revoked grant or changed role cannot be reused as an authorization lease.
Write permission is not required for moderation of a readable board.

The worker retains its global storage/feature guards and existing early checks
to preserve legacy error precedence. Core independently enforces the operation's
authority, even when called directly without the legacy command adapter.

## Persistence and visibility

The target's `extras_removed` flag rejects both operations with
`ARTICLE_WITHDRAWN_TARGET`; restoring an origin withdrawal is never allowed.
The existing combined moderation/extras audit limit is checked using a 64-bit
sum before `moderation_set`. Exhaustion or a failed write returns
`ARTICLE_MODERATION_WRITE`, mapped to the existing uncertain-storage response.

`moderation_set` continues to own append/sync-before-publication, idempotence
and actor/time audit. Core creates no extras D/T record, link event or origin
lookup. Imported and local articles both use the same local journal, independent
of remote authority. This cannot impersonate an origin withdrawal.

Restore clears only the target's local hidden flag. It never clears an
ancestor's hidden/withdrawn state or changes descendants, IDs or parent links.
The existing effective-visibility implementation remains authoritative for
lists, reads, search, unread, WebUI and link references. `ARTICLE_OK` means the
local flag operation succeeded, not that all ancestors permit public display.
The worker still returns `HIDDEN SAVED`, `RESTORED` and the existing error text.

## Verification and documentation

`make test-article-core`, already included in `make check`, directly exercises
both APIs without UI/Session: guest/unknown/stale/suspended identity, owner-only
denial, missing/wrong-board targets, read ACL and password grant denial, revoked
manager role, withdrawn local/imported targets, the combined audit limit and
64-bit overflow boundary, unhealthy stores, local/imported success on a board
without write permission, preserved ancestor concealment, and failed journal
writes without false success or extras/title/body side effects.

The existing hosted integration suite (`tests/test_moderation.py` and
`tests/test_shared_visibility.py`) verifies real persistent idempotence, restart,
short-write handling, inherited SIGOP authority, multi-BBS non-propagation and
Web/read-path visibility. No test conditions or CI gates are weakened. Native
validation remains main-only; report results against the exact main SHA.

Local UCRT GCC direct tests pass with `-Wall -Wextra -Werror`. The full Windows
runtime's known POSIX dependency is outside scope; existing hosted Linux CI
performs runtime verification rather than setting up another test environment.

Reviewed public references: `docs/manual/board-admin.md`, `boards.md`, `links.md`,
`logs.md`, and Japanese/English `help/*/content.txt`. Their existing commands,
authority and visibility explanations still apply unchanged. The implementation
hashes in source-lock are refreshed after this review. Internal C APIs belong
in this development document, not public HELP.
