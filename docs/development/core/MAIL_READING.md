# Mail list, search and reading Core boundary

Baseline: main `589cb70e5dc3cd1b28e423f3a0a46b6e1b6875e9` (PR #91).
This grouped LN-23/24 slice builds on Mail Core composition and extracts listing,
sent/inbox/unread filters, title/body search, timestamp filtering, direct reading,
next unread/latest selection and ascending/descending continuous reading.
It does not change command syntax, stored messages, attachment protocols or UI.

## Contracts and authority

- `mail_core_list` takes an actor and typed MailQuery; returns an owned MailPage
  with at most eight summaries, the actor's total unread count and an exclusive
  continuation cursor. The next cursor remains the ID immediately before the
  next matching message, preserving existing adapter pagination across ID holes.
- All/inbox/sent/unread filtering is participant-only. SEARCH uses existing index
  candidates, checks mailbox participant access before loading any body, then
  applies the exact case-sensitive title/body substring check. An administrator
  has no private-mail override. List/search do not mark messages read.
- NEWS filters received mail with inclusive since/until timestamps; until=0 is
  unbounded. Date parsing and timezone policy remain in the existing adapter.
  Reversed bounds produce no matches as before, not a new parsing rule.
- `mail_core_read` selects an explicit ID, first unread incoming mail, latest
  accessible mail, or the next participant-visible sequence entry. Sequence
  starts are inclusive; descending starts beyond the total clamp to the last ID.
  Zero/exhausted sequence positions succeed with sequence=1 and message.id=0;
  missing direct/unread/latest reads are unavailable rather than successful ends.
- The same current-user/credential, suspension, mail-feature and storage checks
  used by composition run inside both new APIs. UI flags are not authorization.

## Result and persistence ownership

MailSummary owns names/title/IDs/timestamp/read state. MailRead additionally owns
attachments and the next sequence cursor; body storage belongs to the caller.
No mailbox cache pointer escapes Core. A local copy protects metadata while
other store calls reuse caches. After durable marking the row is acquired again
before producing the result, so the returned read state reflects the update.

Existing mark-before-body ordering is preserved: a subsequent body read failure
can leave the read flag committed. It never returns a partial body or successful
read result. All failure outputs, including partial pages and unread counts, are
cleared. A too-small body buffer is refused before marking when stored body size
shows it cannot fit. Invalid/non-progressing search candidates and totals beyond
the existing content-ID bound fail closed instead of allowing a loop/overflow.

Mailbox and index remain the persistence APIs. No schema, migration, new DB
connection or collection-sized in-memory structure is introduced. One page owns
eight bounded summaries; one read uses the existing BoardResult draft-body buffer.
The single worker still serializes authorization, data access and completion.

## Adapter compatibility and remaining work

board.c parses dates, maps typed outcomes and formats existing localized MAIL
LIST/SEARCH/ROW/ITEM/END responses. It publishes read/sequence IDs only after a
successful Core result. Body-storage failures preserve the existing generic
ERR storage unavailable completion clearing. The earlier quota-refusal editor
completion fix remains intact. Read action labels N/T/R and sequence action
translation are preserved; recursion through mail_execute is removed.

Attachment metadata, batch selection and common-buffer import are subsequently
covered by [Mail resources](MAIL_RESOURCES.md). Transfer protocol handling, guest
SYSOPMAIL and login notices remain on their existing paths. MAIL NOTIFY remains Member Core. This does
not claim completion of all Core separation or a new structured Web endpoint.

## Validation

Local C11 -Wall -Wextra -Werror tests pass 18 Mail Core groups, including the 12
existing composition groups. Six new groups cover eight-row paging and gaps,
owned snapshots, guest senders, sent/inbox/unread/date filters, private search
candidates, case-sensitive title/body matches, direct/unread/latest reads,
sequence direction/end behavior, read-vs-sent marking, actor/feature rejection,
partial read/page failures, invalid index cursors and body/query bounds.
These use persistence stubs; they do not establish native runtime success.

`tests/test_mail_reading.py` joins the existing runtime suite. It uses three
accounts (one administrator) and interleaved private messages for two-page
search, no-admin-override checks, date/box filters, unread/latest state, deletion,
ascending/descending sequences and restart persistence. Python syntax passes
locally; execution awaits hosted PR CI and later exact-main native CI.
Existing full suites stay enabled, with no duplicate local regression/native run.

Canonical mail guidance and Japanese/English HELP document the unchanged listing
and read-state behavior. Source-lock hashes are reviewed; canonical generation,
links, search and Markdown/HTML/offline parity are validated before publication.
No production access, release or deployment is performed.
