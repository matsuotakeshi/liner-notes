# Editor documents and article-to-buffer Core boundary

Baseline: main `bb29167308101519168e9f65dab9ce323cf1fd8d` (PR #95).
This LN-23/24 extraction groups private common-buffer operations, long biography
editing and article import. It complements the existing mail import Core API.

## Responsibilities and lifetime

- `member_core_document_get` reads the authenticated member's own biography or
  common buffer into caller-owned storage. `MemberDocument` is a typed choice,
  not an arbitrary profile field or target user. No administrator override or
  UI-supplied privilege flag is accepted. Store scratch pointers never escape.
- `member_core_document_save` handles both save and clear (empty input).
  Biography and buffer limits remain 8,192 and 262,144 bytes. Existing profile
  storage validates UTF-8/control characters and commits the same journal records.
  Authentication, credential generation, suspension and storage health are checked
  for each operation. A returned save failure never starts a successful completion.
- The APIs do not touch article/mail draft revisions, read flags, profile
  visibility, notification settings or credential generations. BIOEDIT edits the
  member's own biography; ordinary public profile viewing stays in MemberProfile.
- Failed document reads clear the full provided output, including post-read
  storage failure, malformed stored values and insufficient capacity. Inputs and
  outputs must not alias. The caller must supply valid, correctly sized buffers.

`article_import_buffer` requires an authenticated actor and an article on the
current board. It reuses Core read authorization and effective visibility, with
inspection disabled even for administrators. Posting permission is unnecessary.
Hidden/withdrawn ancestors therefore prevent import of descendants. Board grants
are checked in the existing policy context and the context is reset on return.

Import copies the body through the caller's existing worker scratch buffer,
validates its length against metadata, and invokes the existing profile store
only after successful visibility/body checks. It does not mark an article read,
alter the source or consume a draft. Scratch is cleared on success and failure.
The adapter emits `ARTICLE IMPORTED TO BUFFER` only after persistence succeeds.
Rejected imports do not replace the previous buffer. A storage write failure can
still require recovery under the existing journal rules; this refactor introduces
no new rollback guarantee for partially failed physical writes.

## Preserved boundaries and remaining work

`board.c` and `board_features.inc` map typed outcomes to existing command replies
and editor completion fields. Session continues to own interactive editing,
append-versus-replace behavior, Undo, confirmation, input limits and helper
lifecycle. `/BUFFER` appends into the current edit; `/TOBUFFER` saves that edit
without ending it. BUFFER CLEAR and BIOEDIT retain their existing replies.

No new command, HELP token, database schema, migration, package dependency or
background worker is added. Article/mail imports still write the same private
profile field. Public documentation only clarifies existing buffer isolation and
read-permission semantics; development APIs are not advertised as public APIs.

All APIs remain synchronous under the single board worker. This change does not
make profile storage thread-safe or finish the full Session/Core extraction.
Mail's previously extracted import path is unchanged and is included in the new
cross-feature socket regression.

## Validation

- Local MinGW GCC C11 with `-Wall -Wextra -Werror`: 54 Member Core and 18 Article
  Core groups passed (six new groups total). Coverage includes own snapshots,
  clear/save routing, byte/capacity limits, invalid kinds, revoked actors,
  missing/partial store results, persistence rejection, hidden/withdrawn imports,
  current-board scope, password grant revocation, administrator denial, read-only
  permission and preservation of the previous buffer on rejected operations.
- `tests/test_editor_documents.py` is included in `make check`. It uses an
  isolated SQLite database and real sockets to exercise biography/common-buffer
  isolation, article and mail imports, hidden descendants, saved article/mail
  drafts, clear and restart. Python syntax is checked locally; runtime results
  are pending hosted CI and are not inferred from syntax checks.
- Canonical manual generation, offline ZIP and validation passed: 42 pages,
  2,812 local links, 172 legacy URLs and Markdown/HTML parity. Reviewed source
  fingerprints, Python syntax and diff checks also passed.
- Full runtime builds, existing UI/HELP/WebUI/Door/storage regressions and new
  socket tests use existing PR CI. Exact-main native CI follows merge. No PR code
  is run on home VMs and no production service/data is changed.
