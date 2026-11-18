# Download services Core boundary

Base: `e2ebcdb49646f8c16ed7c9cf052c218b9d09e87f` (merge of PR #103).
This grouped LN-23/24 increment covers download lifecycle, receipt history and
cumulative counters. It does not claim complete Core separation or a release.

## Responsibilities

- `download.c`: existing FILEOPEN/FILEFETCH/FILEFETCHBLOCK/FILEDONE/FILEABORT parser,
  digest text decoding, Base64 response encoding, error/wire markers and report
  formatting. Existing callers and command strings remain unchanged.
- `download_core.c/.h`: typed open/fetch/finish/abort/history/stats services;
  subject-bound transient tickets, quota/expiry, current ACL/content verification,
  completion admission and owned bounded result snapshots. No terminal strings,
  socket handles, session objects or Base64 encoding in service logic.
- `download_store.c` + `download_repository.h`: existing journal/replay, receipts,
  cumulative counters, per-board retention replacement and startup/shutdown.
  The repository is internal and is not an authorization bypass API.

Ownership remains with the single existing vault worker; startup/shutdown only
open/close while requests are quiescent. IdentityProvider resolves a fresh
Principal for every request. UI/transport callers must not manufacture trusted
subject/capability fields. Core independently checks repository health, the
read capability and bounded subject; vault_info/vault_read_block enforce current
file/board ACLs and the installed live guard. The existing guard is rechecked
before recording a live completion. No new authentication scheme or parallel
repository access is introduced. Inputs must not alias output structs; failures
clear outputs, including a partially populated report or read buffer.

## Compatibility

The parser retains health/read-capability-before-syntax precedence. Report privacy
stays unchanged: FILEHISTORY requires CAP_VAULT_MANAGE; FILESTATS filters through
vault_info and permits managers to see nonzero totals for unavailable files.
It does not return file titles, content or private tokens. History pages retain
8-row / 256-scanned-row limits and continuation offsets; stats retain 8-row pages.
The cursor is an index in retained receipts and can change after pruning.

Tickets stay subject-scoped, 1,024 globally and 16 per subject. Expiry remains
strictly greater than 3,600 seconds from creation when a ticket operation performs
cleanup; reads do not extend expiry. Same-subject sessions share ticket ownership.
Startup loses unfinished tickets. Completed receipts survive restart. Unknown
or another subject's syntactically valid abort stays idempotent and does not
cancel their ticket. Random bytes still come from OpenSSL; tests stub them only
in the isolated unit executable. A backward wall clock retains existing behavior.

Fetch allows rereading received offsets, rejects gaps, returns raw bytes through
the typed API and keeps 768-byte / 65,536-byte connector capacities. Completion
requires all bytes and matching digest/current metadata; duplicate completion
uses retained receipts and current read access/content. The existing duplicate
path checks digest, not a newly invented size or session condition. Retention
can discard duplicate-acknowledgement records while keeping cumulative counts.

`LNDOWN01`, 4096-byte checksummed rows, legacy D/new d protocol records and S
summary rows remain identical. LINE/ZMODEM/XMODEM/YMODEM/YMODEM-G/MLINK/TEXT/
XMODEM-SUM/XMODEM-1K and legacy UNKNOWN replay unchanged. SQLite stream interception
is reused; no schema, IDs, payload files, retention transaction or limits change.

## Existing failure boundary, follow-up

A receipt append/apply can succeed before downloads_prune fails. The command then
reports storage failure and retains the live ticket; the persisted receipt/count
may already exist. This refactor preserves that ordering and tests it explicitly.
It is NOT a transaction rollback. A retry of that still-live ticket can append
again under the existing implementation. Exactly-once completion after a failed
retention step is a separate correctness task; do not describe the ordinary
successful-ack duplicate test as covering that failure boundary. No automatic
retry, DB repair, schema change or deletion is added in this PR.

## Validation and limits

- `make test-download-core`: real Core + legacy adapter with mocked vault/store,
  clock and entropy. Five groups cover denial and zeroed results, partial/replayed
  reads, ACL/content change, final guard/storage failure, duplicate completion,
  cross-subject requests, abort, quotas/expiry/restart, 64KiB boundary, filtered
  history pagination, visibility/counts, error precedence and exact Base64/markers.
- `make test-download-store`: real journal and retention with a policy stub. Three
  groups cover old/new protocol replay, per-board retention/S summaries, totals
  after restart, committed-receipt/prune failure, damaged-tail refusal without
  truncation. The driver runs plain files and authoritative SQLite on POSIX;
  Windows exercises the native file branch only. It does not simulate real disk
  exhaustion or establish production filesystem failure guarantees.
- `tests/test_download_core.py` joins existing test-stage65: real RAW sockets,
  identity/ACL changes, abort/incomplete/complete/duplicate requests, receipt
  replay, history pagination and retention/totals across restart, with both file
  and SQLite storage. Existing stage58 and Web block tests remain enabled.

Local Windows GCC: five Core/adapter and three real-file groups pass; new Python
fixtures compile. Adapter/Core/store/bridge syntax passes with declaration-only
Windows POSIX shims. Full runtime/socket tests and POSIX SQLite tests are pending
hosted CI; native VM execution is main-only. Do not attribute earlier main/PR
results to this branch. No production access, deployment or release is performed.

Canonical manual local verification passed: 42 pages, 7 chapters, 2,812 links,
172 legacy URLs and 622 legacy anchors; Markdown/HTML/offline parity matched.

## Focused CI follow-up for PR #104

Both Linux 9 jobs failed at the newly added socket assertion after removing the
file-board read ACL: the unchanged vault_info returns VAULT_NOT_FOUND, mapped to
`ERR no file`, not `ERR vault denied`. The assertion now requires that exact
existing response; production logic and all other assertions are unchanged.
The user requested only the affected CI. This patch suppresses the automatic full
PR rerun using the commit's skip marker, leaving required checks unsatisfied.
A separate hosted-only verification branch checks out this patch's exact SHA and
runs tests/test_download_core.py in the same Rocky/AlmaLinux 9 containers. That
supplementary result is not a replacement for required full PR checks before merge.
