# Core / UI boundary completion

Base: main a082f9484d0949bdd7953e1cd64631b06c40999d (PR #128).
Scope: the remaining Session state ownership, Core/UI boundary and UI-free
operation contract. No MMM, SSH, authentication Broker, schema migration,
production changes or release are included.

## Ownership and dependency direction

- Domain services (`*_core.h`) expose typed actors, requests, results and owned
  pages. They enforce current account state, epoch and ACL through their existing
  repository/provider ports on the worker thread. A menu flag grants no authority.
- `operation.h` defines the asynchronous request/result data contract and retains
  the BoardRequest/BoardResult aliases for existing adapters. It has no worker
  lifecycle or queue functions. `board.h` exposes the worker implementation only.
- `SessionCore` depends on that data contract, not Session, Ui, the queue,
  terminal, socket or database lifecycle. The event-loop owner supplies an
  enqueue port. Transport/UI adapters submit requests and render completions.
- Unverified login names live in SessionAuth.login. Confirmed identity, epoch,
  member level, admin/SIGOP observations, board/article position and password
  grants live in SessionCore. Password dialogue, editor and rendered page state
  are now private RTBBS state owned by the UI binding; shared Session/runtime
  access typed callbacks and snapshots. See [RTBBS integration](RTBBS_UI_ADAPTER.md).
- Core owns result-driven updates even when a UI handler returns early. Adapter
  code does not assign identity, epoch, selected board/article or grant entries.
  Runtime's account epoch notification uses the same guarded Core update.

## Headless operation lifecycle

1. Initialize a fresh `SessionCore` with `session_core_init`. Set its clock and
   optional structured output flag. An initialized session is a guest.
2. Build an OperationRequest from domain inputs. No terminal command parsing or
   Ui construction is required. The existing internal operation/action values
   are compatible with BoardRequest; they are not a new external wire protocol.
3. Call `session_core_submit` with a synchronous copying queue port. The Core
   replaces actor, epoch, session/slot and password grants; the worker rechecks
   authorization. Accepted requests set pending and preserve auth throttling.
4. Call `session_core_receive` on the trusted worker's result. It validates
   session/slot and active/closing state before any update, atomically applies
   identity/epoch/grants/position, then returns a borrowed typed view. Failed,
   revoked and ignored results return no content/page/effects pointers. Never
   retain that view after the worker result buffer is reused.
5. `keep_position` only suppresses navigation for a cancelled read. It cannot
   suppress authentication changes, revocation or grant validation. The legacy
   adapter uses this to preserve existing scan cancellation behavior.
6. Logout refuses while a request is pending. Successful logout clears actor,
   epoch, name, level, permissions and selected position. Revoke clears those
   values and ends pending/wait state immediately. Abort prevents further
   operations but preserves identity until disconnect preferences are saved;
   final adapter disposal securely erases all state. Door/binary/transfer
   revocation invalidates first, then aborts.

Only the trusted owner and worker exchange these structures. Arbitrary network
clients must not supply OperationResult or Core state. The worker queue remains
one outstanding operation per Session; there is no new distributed RPC or replay
protocol. A future network transport must authenticate and frame its own input.

## Typed results and compatibility

`operation_pages.h` copies authorized, bounded domain results into a tagged union:
article lists/search/news/FIND, tree, unread, notes, boards, article audit; mail
lists/search and uploaded attachments; members/applications, profile searches
and privacy-filtered profile detail; vault files/search/news, details, child
boards, board/category lists and audit pages. Cursor and count semantics remain
those of each domain result. Article/mail bodies still reuse the existing body
buffer. Pages are opt-in; legacy requests keep their existing text output.

This is the asynchronous projection of existing domain services, not a new HTTP
API. Other domain operations retain their typed Core functions and typed effects
in OperationResult. RTBBS presentation, RAW framing, localized prompts and
current WebUI parsing remain adapters. Converting all WebUI screens or adding
other UIs is not required to call Core independently and is not claimed here.

Article access recording now consumes an explicit read/post effect rather than
matching ARTICLE/SAVED display strings. Failed read-audit persistence erases
structured content/page/body and marks failure, while a durable post with an
audit failure retains its saved ID and carries an audit_failed warning. Existing
text responses remain compatible. Vault revocation clears typed pages before
publishing a completion.

No database schema, saved ID, command, configuration or installed path changes.
Internal C struct layouts change: rebuild all consumers together. No binary ABI
compatibility with separately compiled old adapters is promised. The bounded
page union introduces no per-record persistent allocation or whole-database load.

## Verification

- Pure Core tests: stale session/slot, failed result payload suppression, cancelled
  selection, grant bounds, monotonic password epochs including GROUPMOVE,
  pending logout refusal, failed auth cleanup, revoke and disconnect ownership.
- Adapter regressions: unverified login separation and Core effects surviving
  an early-return profile UI handler, plus existing RTBBS/RAW/JA/EN coverage.
- Real-store headless tests: registration, post/read/seen, structured list/search/
  FIND/boards/profile/mail, legacy output compatibility, ACL rejection, and
  fresh-process login/article/mail persistence without manual identity apply.
- Boundary tests reject worker lifecycle imports into SessionCore, direct adapter
  identity/grant writes and audit decisions inferred from rendered text.
- Existing Hosted regression/package/manual CI is mandatory for the candidate.
  Native checks execute only after trusted-main integration. Windows syntax/unit
  checks alone are not claimed as POSIX runtime verification.

## Completion boundary

The Session adapter split, explicit state owners and UI-independent submission /
completion contract are implemented together with domain service APIs. Future
MMM/Web/SSH adapters and Door Broker may use those boundaries without duplicating
ACL logic. DB normalization and new transport authentication remain separate
approved workstreams, not unfinished Core extraction tasks. Validation is only
complete when the candidate CI succeeds; record its SHA and runs in the PR.

## PR #129 FIND fixture correction

Commit 35792eaca3b16fed3405a7170d475e35058d24de failed Linux compatibility
run 44446382247 on Rocky 9 and AlmaLinux 9 at `find-page`. The added headless
fixture left the author empty. `article_core.c:validate_filter` requires an
explicit author or `*`, as exercised by the existing article Core tests; SEARCH
has a different input contract. Keep the empty-author request as a rejection /
no-payload assertion and submit `*` for the successful FIND page. Assert success,
page count and the original article ID. No runtime code, input validation,
permissions or timeouts are changed. Actual corrected POSIX integration remains
subject to Hosted CI for the new commit, not the preceding successful unit tests.
