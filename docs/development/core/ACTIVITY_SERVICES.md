# Activity services Core boundary

Base: `e555617293d1f65a1188ca3e414c2fb98bfef0f2`, the merge of PR #102.
This is a grouped LN-23/24 refactor, not completion of Core separation.
No production action, service stop, release or DB migration is included.

## Responsibilities and trust

`activity_core.c/.h` admits Gather reads/posts, history/stats/table requests and
connection inspection/announce/disconnect/shutdown/cancellation. Identity and
epoch must come from the trusted session, not a client-supplied administrator
flag. Every call checks current member identity, suspension, epoch and healthy
member/moderation storage. Gather operations reject closed/broken activity storage even for an empty list;
Gather snapshots additionally check profile health.
Core returns enums and owned bounded structs; it never formats terminal output.
Inputs may not alias outputs. All output fields clear on failure.

Gather retains the existing UTF-8 validator, 70-codepoint/280-byte line boundary,
CRLF input and ten-line limit, including blank lines. It returns one copied row
at a time with sequence/actor/time/name/handle/body and a next offset. No borrowed
profile/database pointers escape. `cursor == count` is successful end-of-list;
`cursor > count` is invalid. Cursors are offsets in the retained list, not stable
IDs or snapshots across mutations. These existing semantics are unchanged.
The adapter retains the previous byte-budget pagination, dates, language strings
and wire markers. Gather journals use durable sequence numbers, not cursor IDs.

Userlog requests resolve zero to the current member. Other-member/ALL requires
actual global administrator status, or the existing startup public/RTBBS member
policy. Those policies never grant access to guests or admin operations. The
runtime owns the live connection ledger and all report rendering, as before.
This PR separates authorization, not the entire userlog implementation.

`activity_control` authorizes global administrators, validates kind/body and
commits the audit request before returning an effect. CONNECTIONS only authorizes
inspection and does not append an event. The runtime still owns current sockets,
notification delivery, disconnect and shutdown clocks. Results are internal,
trusted, immediate, one-use requests, not transferable authorization tokens.
A queued result must never be exposed as a reusable public capability. The
existing board-worker/completion architecture and completion ordering are not
changed here. Invalid/missing target connections are detected in runtime after
recording acceptance; audit presence does not mean the effect succeeded.

## Persistence and lifetime

`activity_store.c` owns the existing lifecycle and bounded retained-message cache.
`activity_repository.h` is a worker-internal persistence seam, not an ACL-bypassing
public service. Runtime startup/shutdown opens/closes storage without concurrent
requests; all live calls are serialized on the existing board worker. Startup
replays the existing journal and validates existing actor references and Gather
text. No new thread, lock, schema, opcode, public command or ID is introduced.

`LNACTV01`, 4096-byte checksummed rows, G/A/K/S/C kinds, actor/target/time/sequence,
append-before-ack and current damaged-tail refusal are unchanged. Existing SQLite
stream interception remains in `durable.h`/`storage_sqlite.c`. Gather's retention
limit controls the in-memory/display window; it does not reclaim the journal.
Changing the limit requires the existing startup option, not live cache mutation.

Catalog services and Article Core SIGOP assignment already used by activity.c
are retained and not reimplemented. Runtime state, session parsing, confirmation,
report continuations, notices, UI and transport code are unchanged.

## Verification

- `make test-activity-core`: direct-service identity/epoch/storage/role rejection,
  Gather bounds and copied snapshots, cursors, all three log policies, global
  control admission, audit-before-effect and empty outputs on persistence failure.
  It stubs repository and identity dependencies; chat's UTF-8 implementation is
  covered by existing chat tests and the new real-socket Gather test.
- `make test-activity-store`: real journal creation, durable ordering, bounded
  retention, reopen with different limits, disabled retention, closed writes and
  corrupt-tail rejection without truncation. Identity/text replay dependencies
  are stubbed; it does not simulate disk-full or claim actual fsync-failure testing.
- `tests/test_activity_core.py` is added to existing `test-stage65`: unchanged
  RAW command responses, Gather UTF-8 limits/cancel/restart, report privacy/public/
  compatibility modes, guest rejection, runtime inspection/announce/disconnect/
  scheduled shutdown/cancellation, in both journal and authoritative SQLite modes.
- Existing stage57/stage58/UI tests remain enabled in existing CI. No duplicate
  full-suite or native execution is started from this PR worktree.

Local Windows checks: both new C test executables pass (five Core groups, two
real-journal groups); the store test uses the Windows durable-file branch, not
SQLite interception. Adapter/Core/store/board syntax passes with declarations
for Windows-missing POSIX types only. The full runtime and new socket test require
hosted CI; none of the native OS tests have run for this branch yet. Canonical
manual generation/link/search/offline checks are run before PR submission.

Local manual result: 42 canonical pages, 7 chapters, 2,812 internal links,
172 legacy URLs, 622 legacy anchors; Markdown/HTML parity passed.
