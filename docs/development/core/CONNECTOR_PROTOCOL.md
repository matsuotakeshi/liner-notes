# Connector protocol boundary (LN-23/24)

This unreleased refactor builds on connector buffer/binary Core and extracts the
remaining negotiated-line adapter from Session. The Web connector now also
negotiates `core_views: 1` for typed reads. Older peers continue using the
legacy RAW view requests.

## Structured Web reads

`CONNECTOR VIEW` accepts only the named read operations in the protocol manual.
There is no wire field for the worker opcode, actor, authentication epoch, or
board grants. Session supplies them through its existing single-pending-operation
queue. Board selection and password prompts still run through the existing session
flow. The worker projects authorized pages and bodies into operation results.
The adapter returns those values with the caller's request nonce, after applying
Core result status and navigation effects. An error or hidden article has no
structured content. Mail attachment metadata is collected using participant ACLs.

The HTTP and browser response shape remains compatible. Browser attachment rows
use structured metadata; the legacy connector converts only attachment headers,
never rows embedded in the message body. The terminal relay and dedicated file
transfer protocol remain available.

## Structured Web updates

`core_updates: 1` advertises a token-bound `CONNECTOR UPDATE` block and
`input_limit` advertises the maximum wire line size. Its ASCII fields are
`nonce|operation|board|id|arg|flag|attachments|recipient|title|body`.
The nonce is 32 lowercase hex digits. IDs and flags are decimal, attachments
are comma-separated IDs, and the final three fields are UTF-8 bytes in
lowercase hex. An empty field represents no value. Compose uses a draft flag,
CRLF body lines and a board or reply target as appropriate. The form action
flag is the user's explicit confirmation for operations that require it.
The adapter checks an allowlist, the active dialogue and request bounds.
Only the session can supply the actor, epoch and password grants.

The board worker resolves mail recipients/replies and uses the same Mail Core
write path for typed and terminal inputs. Ownership checks and the saved
revision are enforced as part of sending or saving. An active editor's body,
target and revision stay intact until its update succeeds. Chat sends take the
private target from the active conversation, and acknowledge after Chat Core
delivers or saves the message. A lost private target never defaults to public
chat.

The response supplies success, the resulting article/message ID or draft
revision where applicable, an article audit warning, and display text. The Web
bridge correlates it by nonce, then uses SYNC for the resulting session state.
It does not retry an update after a missing or invalid response. Older backends
without the capability use the existing terminal flow. Navigation, dialogue
entry, credential prompts, terminal controls and dedicated file transfer retain
their existing protocols.

## Web line conference

The additive `core_conference: 1` capability enables the Web line-conference screen.
`conf_rooms`, `conf_history`, `conf_unread`, and `conf_position` use the existing
VIEW envelope. The numeric arguments are room and cursor. The projected
`conference` object includes room, next, through and either rooms or lines.
The UPDATE operations `conference.post`, `conference.seen` and `conference.create`
use id for room, arg for the seen cutoff, and body for a message or room title.
The Core resolves the caller's identity and applies the same member and
administrator permissions as RTBBS and MMM. Displaying an unread page has no
write side effect. Older runtimes omit the capability and the Web entry stays hidden.

## Responsibilities and lifetime

- `connector_protocol.h/.c` recognizes OPEN/SYNC/LITERAL/BLOCK/LINKBLOCK/DATA/TRANSFER
  envelopes, decodes LINK requests into `LinkWireRequest`, and serializes a
  `ConnectorState` snapshot. It depends on C/OpenSSL and existing ID limits, not
  Session, UI, workers, storage or authentication.
- `session.c` owns the marker, selects the current state, checks negotiation and
  authentication/editor/password/chat/gather restrictions and link membership,
  and maps a parsed request into BoardRequest. Board/Link Core continues to check
  authorization, ACL, origin and event semantics. Parsing grants no authority.
- Envelopes borrow bytes from the input until modified. The marker is absent or
  exactly 64 bytes plus NUL. Envelope identification uses the explicit length;
  payload conversion to numbers/commands is performed only for terminated input.
- LINK parsing mutates its terminated input, copies metadata into a small result,
  and decodes directly into the caller's body buffer. It avoids a second 256KiB
  stack copy. Failure zeroes the result and empties the output. Decoding scratch
  is freed before returning. Buffer capacities are checked and Session asserts
  metadata field compatibility at compile time.
- Snapshot formatting borrows a validated login name and nonce only for the call.
  It returns no partial successful response. Session aborts on output overflow.
  It includes no password, input body, or negotiated marker.
- These functions have no shared mutable state, worker access or asynchronous
  ownership. The runtime scheduler still applies the previous worker result
  before dispatching the next request. Block/binary lifetimes remain in
  Connector Core and Session disposal.

## Compatibility and trust boundaries

Command order, case, prefix lengths, lower-case marker/nonce validation, accepted
Base64 forms, lookup token handling, numeric upper bounds and protocol 1 JSON
field order remain unchanged. LOOKUP still leaves identifier semantics to Core.
The link event signature/synchronization protocol 2 is unchanged. Invalid LINK
requests use the existing syntax error; denial precedes parsing. Local HIDE and
origin withdrawal visibility remain in existing Link/Article Core.

The fallback for a pending LITERAL remains after envelope dispatch: SYNC can
inspect secret/editor state without consuming user content; the next literal
line rechecks editor eligibility. A wrong marker does not authorize a connector
request. Recognizing a marker alone is not authentication.

## Validation

`make test-connector-protocol` checks every envelope, missing/wrong markers,
truncation, nonce/case rejection, every LINK action, ID/time bounds, malformed
Base64, forbidden controls/NULs, Japanese metadata, exact 256KiB body capacity,
failed-result clearing and byte-exact state JSON including output size bounds.
`make test-adapters` adds Session-level unauthenticated LINK/BLOCK rejection,
SYNC during secret input and LITERAL state revalidation.

The new direct target is part of existing `make check`. Existing WebUI connector,
block transfer, binary transfer and Link integration tests remain enabled. Local
Windows validation covers the pure C test, strict syntax checks of Session and
adapters, i18n catalog checks and canonical manual. Full adapter/runtime and OS
integration are left to existing hosted PR CI and trusted-main native CI; local
syntax success is not an execution result for those tests.

The additive `core_search: 1` capability enables paginated Core search views.
`VIEW find <board> <cursor> <scope> <author> <query>` uses `HERE`, `SUBTREE`, or `ALL`.
Board 0 selects the current board. The caller can pass a fixed board ID to keep
the scope anchored across requests. An asterisk selects any author or query.
`VIEW mail_search 0 <cursor> <query>` searches mail visible to the signed-in member.
Cursor 0 starts the search. Reuse the returned `data.next` with the same arguments
until it is 0, including when a page has zero matches. Article rows are returned
in `data.matches`, and mail rows in `data.mail`. The Core applies its normal
visibility, password grant, and mailbox access checks. Older runtimes continue
using the legacy command path.

`core_discovery: 1` adds the typed views below. Position 0 starts each list.
Pass the returned `data.next` as the next position with the same arguments.

| View | ID | Trailing argument | Result list |
| --- | --- | --- | --- |
| `board_news` | Board ID or 0 for current board | `YYYY-MM-DD` | `matches` |
| `mail_news` | 0 | `YYYY-MM-DD` | `mail` |
| `mail_inbox`, `mail_sent`, `mail_unread` | 0 | None | `mail` |
| `file_boards` | 0 | None | `file_boards` |
| `file_search` | 0 | Pattern, optionally prefixed by a board and a pipe | `files` |
| `file_news` | 0 | Board, pipe, and `YYYY-MM-DD` | `files` |
| `profiles` | 0 | Search text or `*` | `profiles` |
| `profile_find` | 0 | Uppercase field key, space, search text | `profiles` |
| `profile_group` | Member group ID | None | `profiles` |

For file searches a board of `-` means the root. File lists and subtree
lists reuse the existing `files` view. The shorter legacy commands remain
available when the peer has no discovery capability. Date scans here are
read-only queries. The interactive `CHECK` workflow owns checkpoint changes.

`core_operations: 1` adds the following authenticated, typed views. Applicable
access and visibility are enforced by the same worker operations as the terminal.
Use the result's `data.next` to resume a paginated view.

- `article_unread`, `article_new`: optional board ID; `article_prev`,
  `article_next`, `article_parent`, `article_child`: current article ID.
- `article_scan` uses the board ID or 0 and `ASC|DESC scope author query`.
  `mail_scan` uses ID 0 and `ASC` or `DESC`. Both return an item and the
  next item cursor with the same filters and direction.
- `mail_next`: next unread, ID 0; `mail_previous`: current owned mail ID;
  `mail_files`: owner upload list, ID 0.
- `file_children`: file-board path; `file_detail` and `file_text`: file ID.
  For `file_text`, the cursor is a byte offset, and the existing UTF-8/control
  filtering is applied before rendering. `file_audit` reports local changes.
  `file_history` and `file_stats` report authorized download events and counts.
- `member_list`, `applications`: ID 0; `application`: member ID;
  `article_audit`: optional article ID or actor login; `preferences`, `gather`:
  ID 0. `profile_login` takes the optional profile login as its trailing argument.
- `connections` returns the current local connections for an administrator, ID 0.

`check_start` accepts an optional anchored board ID, cursor 0 and the existing
ARTICLES, MAIL or FILES scope. The connector holds the browsing window for that
authenticated session. `check_next`, `check_done` and `check_cancel` take zero
ID and cursor. The result includes `data.check.active` and `data.check.finished`.
Only a completed traversal can commit its checkpoint. Cancelling or ending
the connection discards the traversal.

`core_session_controls: 1` additionally advertises these typed operations:

- Views: `chat_online`, `chat_room`, `chat_prefs`, `chat_status`,
  `batch_list`, `userlog`, `userstats`, `upload_status`, `mail_upload`,
  `article_draft` and `mail_draft`.
- Updates: `chat.receive`, `chat.mute`, `chat.unmute`, the
  `file.batch_*` selection/copy operations, `file.upload_cancel` and
  `mail.upload_cancel`, and terminal preferences (`settings.lang`,
  `settings.pager`, `settings.dimensions`, `settings.editor`,
  `settings.checkmode`, `settings.protocol`, `settings.menu`, `settings.load`).

Upload lookups authenticate each request and use the separate vault and mail
attachment stores. Status is returned in `data.upload`. Cancellation requires
confirmation. Existing transfer chunks and receipts retain their dedicated
connector protocol. Restored drafts return the editable body and authorized
attachment metadata in `data.draft` on the same connection that opened the
editor. Batch copying checks each underlying file operation and preserves
individual copy receipts on partial failure.

`core_runtime_controls: 1` adds the `usertable` view and the
`admin.announce`, `admin.disconnect`, `admin.shutdown`,
`admin.shutdown_cancel`, and `admin.shutdown_now` updates. The Activity Core
authorizes each operation and retains its existing audit behavior. Updates
require the confirmation flag. Shutdown seconds are limited to 1–86400
for a scheduled shutdown. The `usertable` result reuses the existing session
ledger and return cursor.

The Web HTTP adapter exposes `POST api/secret` for an active password prompt.
It checks the same session, CSRF token, and single-use prompt challenge before
passing the password or cancellation to the existing BBS password dialogue.
The terminal and the Web sign-in form keep their own existing entry points.
