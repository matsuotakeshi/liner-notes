# Shared-board connector Core

Base: `cd2028d6154fccfe641ccfa5e2ef578220df9d9c` (PR #107 merge).
This LN-23/24 increment groups local connector metadata/body reads, provenance
lookup, imported posts/title changes/withdrawals and checkpoint application.
It does not implement SSH, Door authentication, MMM or a new link protocol.

## Boundaries

- `link_core.c/.h`: typed read/apply operations, live authorization, event
  sequencing, immutable-checkpoint checks, tombstone bootstrap and durable-write
  coordination. Results contain owned metadata and no terminal response text.
- `link_adapter.inc`: maps existing BoardRequest fields to typed calls and
  formats the unchanged LINK ITEM/END/LOOKUP/APPLIED and error responses.
- `board_link.inc` and the import-post hook in `board.c`: owner-thread SQL,
  article storage, allowlist, effective visibility, body reads and recovery.
  Repository interfaces have no BoardRequest/BoardResult dependency.
- `scripts/link_bbs.py` and the existing link service: peer/signature checks,
  local connector transport, queue/receipts, checkpoint/snapshot exchange.
  These components, protocol 2 and Ed25519/TLS are unchanged.

Core calls require the trusted board worker with already opened stores. They
are synchronous and are not safe to invoke from parallel UI/transport threads.
The actor comes from the authenticated session, never from event author/origin.
Every call rechecks store health, suspension/epoch, the exact board/account
allowlist and read ACL; writes also require write ACL, a content node and v4
article storage. Board-password grant context is installed from the trusted
actor. Existing connector parsing, token requirements and loopback restrictions
remain in Session/transport; this Core is not an unsigned peer ingress.

Requests borrow strings/grants through return and must not alias output buffers.
Reference/applied/page outputs own their values. Snapshot body uses a caller-owned
buffer (the adapter reuses existing result scratch, cleansed before delivery).
No additional full-body copy is used for tombstones: their request substitutes
constant placeholder title/body pointers. Only snapshots of live items read a
body. Metadata batches remain 32 items; one-item scans and end markers are
unchanged. Partial page failures clear the entire result/body, not a prefix of
earlier rows. Metadata retains IDs, parentage, provenance and displayed author
for synchronization; hidden/withdrawn title and body never leave Core.

## Durable application and compatibility

1. Resolve board/origin/article and require a completed prior intent.
2. Check posting permissions and revision bounds before duplicate acceptance.
3. Old/equal revisions acknowledge the existing revision without mutation.
4. Require predecessor sequence except C/W checkpoints. Existing posts cannot
   be created again. Withdrawn articles accept only later W tombstones, never
   a new C checkpoint restoring content.
5. Existing C checkpoints must match parent, creation time, origin alias and
   body hash. Missing W checkpoints create a placeholder without old content.
6. Validate next ID/capacity, persist intent, append if needed, then recover and
   mark the intent done before reporting success. Revision updates retain the
   original immutable metadata through the existing SQL upsert.

Remote imports retain existing exemption from per-local-user post counts;
global, board and body limits still apply. Hidden parents may receive imported
children; effective visibility keeps those descendants hidden. No synthetic
child deletion events, renumbering or schema changes are introduced.

SQL/body/recovery failure retains the existing sticky failure state and restart
inspection error. Clearing an output is not a storage rollback. A committed
intent can survive a failed append; recovery decides whether it can finish or
remove an uncommitted next-ID intent. Callers must retain queued events until a
completion receipt. Startup recovery and imported-article ownership checks are
reused, not reconstructed in the UI.

Typed callers also receive validation of bounded fields, control characters and
timestamp range that the legacy parser already enforces. The Core does not
authenticate a remote author merely because an origin string has valid syntax.

## Verification

- `make test-link-core`: six direct groups using the real Core/hash and storage
  doubles: live authorization, lookup failure, page/body non-disclosure,
  revisions/checkpoints, tombstone creation, durable-write failures. Included in
  `check`.
- `python3 tests/test_link_core.py ./runtime`: journal and authoritative SQLite,
  real authenticated connector, non-allowlisted admin refusal, mixed-origin
  three-level tree, HIDE/RESTORE vs withdrawal, immutable conflicts, old-event
  replay, 32-item page boundary and startup replay of completed effects.
  Included in `test-stage73` with existing signed three-BBS ring/star tests.
- Existing `test_stage74_link_bbs.py` and `test_shared_visibility.py` retain
  checkpoint/compaction/new-node, signing, web visibility and traversal coverage.

Local Windows UCRT compilation passed six direct groups and strict syntax
checks; Python compilation and canonical manual checks are separate. Real BBS
integration runs in hosted CI; home-native machines are trusted-main only.
Do not claim a pending native run or an older main SHA as this PR's validation.
