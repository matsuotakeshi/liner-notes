# Structured article news pages

Baseline: main `12649b49788fb5a1dc95709f701e67188f47bae5` (PR #65).
This LN-23/24 slice extracts the NEWS listing used by dated article news and
CHECK ARTICLES. FIND/SCAN and session/UI profiles remain separate work.
It does not complete the Core/UI refactor or add a public network API.

## Contract and ownership

`article_news(actor, board, scope, start, since, until, out)` returns an owned
`ArticlePage` and `ArticleOutcome`. Scope is HERE, ALL or SUBTREE. Up to eight
copied ID/board/parent/title rows are returned after scanning at most 256 IDs,
including excluded rows. `total=0`; `next` is the next unscanned ID or zero.
An empty page with a continuation is valid. No body is loaded or returned.

Creation timestamps are compared as unsigned values: since and nonzero until
are inclusive; until=0 has no upper bound. Unknown creation time (zero) is
excluded. A reversed interval yields no rows, as before. Date parsing and
timezone conversion remain in the adapter; CHECK's upper bound and checkpoint
commit/cancel policy remain in Session. Listing never marks articles as read.

start=0 means ID 1. Cursors beyond the current count return an empty page, up
to CONTENT_ID_MAX+1; larger values are rejected. IDs and persistence are unchanged.
The adapter retains the NEWS scope/date header, four-column rows and END NEXT.
The dated command still accepts HERE/ALL; SUBTREE comes from CHECK ARTICLES
SUBTREE. No NEWS SUBTREE command is introduced by this refactor.

All calls are synchronous on the existing single-owner board worker. Actor and
password grants are borrowed for the call, never retained. Output must not alias
inputs. This API is not a transport authentication boundary or a concurrent store.

## Authority and errors

Every call rechecks store health, authenticated identity/epoch/suspension and
current read/password access. Guests may list public articles. HERE requires
read access to the requested board; ALL/SUBTREE check each result board. An
unreadable parent does not automatically deny an independently readable child,
and management authority does not bypass read access or ancestor visibility.

Scope/ACL filtering precedes effective visibility and publication. Hidden or
withdrawn ancestors suppress their descendants through the existing repository
visibility check. No titles or counts of concealed results are returned. Core
does not generate link events or change local moderation flags.

Invalid metadata, visibility failure or impossible repository count fails closed
and clears the entire output, including already collected rows and continuation.
The worker's existing lifecycle guards and storage-failure response remain in
place. Password context is cleared on every return. Runtime recovery is not
changed and this is not a database repair routine.

## Validation and documentation

The existing test-article-core target includes direct tests for scope membership,
guest/member/private board separation, parent versus child permissions, local
password grants and their revocation, hidden parent/child results, inclusive
dates and unknown/reversed intervals, owned rows, both pagination limits,
invalid cursors/identity/store health and partial-output clearing. All eight
Core test groups pass locally with UCRT GCC, C11 and -Wall -Wextra -Werror.

test_news_contact.py adds HERE/ALL row metadata and continuation assertions;
Python syntax passes locally. Actual socket/runtime execution is delegated to
the existing hosted make check; no unreviewed code runs on home CI machines.
Native tests remain trusted-main only. No Windows runtime, full regression,
release, or production validation is claimed by the direct C tests.

Reviewed docs/manual/article-search.md and Japanese/English read/unread HELP.
Commands, dates, scopes, cursor/checkpoint semantics and ACL guarantees are
unchanged; public wording is retained. The three changed implementation headers/
sources are refreshed in source-lock after this review. The canonical manual
generator/checker and search tests validate the resulting documentation bundle.
