# LN-28: schema 5 normalization candidate

Base: main ff8b6c1f44efa454a582c25f1c3ff52272d1a6f6, incorporating PR #134's
schema-4 mail relations. This branch is a development candidate, not a release.
No production conversion or in-place upgrade is authorized or implemented.

## Coverage inventory and responsibility

| Domain | Authoritative representation | Decision |
| --- | --- | --- |
| Articles | article_records + article_format | IDs, parent, board, author, time, title/body are named columns; format bit/checksum separated |
| Mail and drafts | mail_records + mail_format + mail_attachments | Ordered unique attachment IDs, event identity and FK/cascade; no IDs embedded in attributes |
| Members, password/reset, lifecycle | member_records + named member_* relations | Salt/hash and login occur only in parent columns; iterations, actor, time, removal reason are typed |
| Member UI, mute, notice, draft/read state | member_terminal, member_preferences, member_mute, member_mail_notice, member_draft, member_read_state | No opaque auxiliary payload; independent event types, fixed ownership and lifetime |
| Member groups | member_event_groups + member_group_change/member_group_move/member_level | One group per row; stable group/member identities and original event order |
| Profile and common buffer | profile_events | Existing typed event history remains authoritative; text chunks and visibility events preserve interrupted-edit semantics |
| Board access, download, USERLOG, chat/Gather, activity, policy/catalog/SIGOP/move/lock/moderation/content | Thirteen existing event tables | Preserve actor/target/time/sequence/context and text; no second mutable copy |
| Public vault and private attachment vault | vault_records, each with its own stream ID | File ID, size, digest, name, description, publisher, category, actor, text, event time/checksum are columns; ID namespaces stay separate |
| UI policy preference, registration notes, checkpoints, delegated groups, board deletion/move recovery, link article mapping | Existing feature tables | Already relational; all are included in logical export/import (including ui_preferences) |
| BBS link daemon | Separate relational link DB | Keep signed canonical envelopes and replay/checkpoint keys; byte-identical signatures must not be rewritten by core migration |
| Web sessions/transfer spool, Door Broker/game DB | Separate ownership and lifecycle | Do not move short-lived secrets/game state into the BBS DB; stop related writers and back them up in their own boundary |
| SSH authorized fingerprints | Explicit administrator file | Configuration, not duplicated member state; retain its existing owner and permissions |
| Search/read/account indexes | Rebuildable SQLite projections | Not authoritative schemas; retain bounded caches and current invalidation rules |
| File content | External payloads | Do not place GiB files in SQL or combine public/private namespaces |

This completes the named-field scope of this candidate. It does **not** replace
event sourcing with mutable current-state tables or collapse service trust
boundaries. Generic history text is the operation's text, not a duplicate state.
A historical message/member ID may have multiple events, so uniqueness is on
(stream,offset), not record_id. Cross-namespace IDs, guest ID 0, deleted entities
and tombstones must not be constrained to a guessed current-state table.

## Versioning and schema

New structured databases use version 5. Versions 1/2/3/4 retain their original
read/write codecs when opened; there is no automatic migration. Metadata and
schema creation for a new version-5 DB commit together. Schema 4 remains readable
although it was an unreleased intermediate candidate.

For the first three domain tables the old attributes column is empty in version
5. Typed child tables own every formerly encoded field; the C bounded codec and
Python exporter reconstruct the portable record on demand. The single declarative
field list is scripts/normalized_fields.py; its checked-in C descriptors are
regenerated with --generate and checked with --check in CI. SQL types/ranges,
foreign keys, per-event uniqueness, subtype checks and cascade cleanup are
validated. Member groups and mail attachments are relations, not packed lists.

The remaining blocks table is an explicit **compatibility escape hatch**, not
used for current-format canonical BBS streams. Historic RTBOARD1/2/3 records and
unknown extension streams are retained byte-for-byte. Rewriting their format
would change the stream's bytes/checksum and is not part of normalization.
Noncanonical reserved bytes in a known record cause copy conversion to fail,
not silently disappear. Keep the original and inspect such data before conversion.
The migration report exposes typed_streams and compatibility_streams, so residual
storage is visible rather than claimed normalized. No formats are retired.

## Copy conversion, atomicity and recovery

1. Stop the BBS and related writers. Retain the original binary/configuration/data.
2. Create and verify db-backup outside the instance root.
3. Use db-structure to a previously nonexistent separate directory.
4. Convert via the same C codec used by the runtime. Copy settings with their
   original keys; compare every logical stream's SHA-256. Run integrity_check,
   foreign_key_check and the normalized-relation audit.
5. Checkpoint WAL, close the connection, fsync the candidate and atomically link
   it under the final filename without replacement. A source, verification,
   space or publication failure cannot publish an incomplete final DB.
6. Retain the .liner-maint-incomplete marker on failure. After interruption use
   a fresh output directory; do not remove the marker and run the failed copy.
7. Verify the copy's actual operations using a separate process/loopback port.
   Source hashes must remain unchanged. Switch production only by separate approval.

Rows, subtype metadata, groups/attachments and stream length/revision use the
same SQLite transaction. Missing or extra relationships, invalid identities and
checksum mismatches fail closed. Logical backup/restore preserves structured
layout, rebuilding a current version-5 copy, including older structured backups.
Rollback means restoring the **old DB and binary together**, never changing the
schema number or running an old binary on version 5.

Backup root must include configured payload/config data, or they must be backed
up separately. A core DB lock alone does not stop independent Web/link/game
writers. Existing stopped-instance backup rules apply; no new cross-service live
snapshot guarantee is claimed.

## Validation and completion gate

- C codec: schema 2/3/4/5 mail, 0..16 ordered attachments; every member event,
  groups 1/32/33/96/100, fixed/maximum 256 KiB articles and every vault event.
- Independent Python export of actual C-generated DBs is byte-identical.
- Missing/tampered relations, invalid IDs, duplicate/FK/range/cascade checks,
  actual SQLITE_FULL and transaction rollback, failed/no-clobber publication.
- Existing Hosted/native migration test now covers sources 1/2/3/4/5, public
  settings, private attachments, actual reads, restart, post-migration writes,
  backup/restore, unchanged source hashes and live-source rejection.
- Existing structured load test now covers 12,000 articles + 12,000 mail,
  search/SEEN/restart/monotonic IDs and RSS growth <48 MiB, once via make check.
- Existing crash/restart, maximum body, compactors, shared boards, Web/Door and
  package tests remain enabled. New normalizers are included in package manifests.

Local Windows codec/export checks are supplemental. Hosted CI and the same-SHA
post-merge native/package results are required before declaring OS acceptance
complete. No release or production application follows from these tests.

## Local evidence (2026-11-20)

Windows/UCRT GCC -O2 -std=c11 -Wall -Wextra -Werror with SQLite/OpenSSL:
- test-storage-relations: passed (schema 2/3/4/5, all attachment counts).
- test-storage-normalized: passed (member/article/vault codecs and faults).
- test_structured_relations.py: 1 passed.
- test_storage_normalized.py: 4 passed; 1 POSIX publication test deferred to CI.
- test_doctor.py: 26 passed, 8 existing platform-specific skips.
- Canonical manual: 42 pages, 2,822 links, 172 legacy URLs, parity valid.

The five-source migration matrix and 12,000+12,000 runtime/RSS test are added to
existing CI, not reported as executed locally. The Windows skips are not OS
acceptance. CI evidence is associated with the final PR head, not this base SHA.

## Migration fixture correction (2026-11-20)

Linux compatibility run 44670067604 at aee926d failed on both Rocky and AlmaLinux
at the UI-preference assertion in test_stage73_database.py. The fixture sent mail
and then requested UI selection without leaving mail. The existing UI boundary
correctly refuses this with a human-readable busy response; the fixture accepted
any response without an ERR prefix and never saved the preference.

The fixture now exits mail explicitly, requires the exact save acknowledgement,
and checks the source setting before conversion as well as after conversion and
restore. The socket UI test also checks refusal in mail, no saved preference on
refusal, and successful saving after leaving mail. Runtime UI restrictions and
DB migration criteria are unchanged. These revised socket tests require the new
Hosted CI result; the earlier failure is not considered a migration pass.

Follow-up run 44670873414 at 46586ef reached the exact acknowledgement check
and exposed another precondition: posting sets the RTBBS classic area to BOARD
even for RAW clients (session_impl_article_result). Closing mail sets UI_HOME
and clears mail mode, but does not clear that selected board context in RAW.
The initial correction was therefore insufficient. Migration preferences are
now saved immediately after login, before posting. The UI socket test retains
the board/mail rejection case and verifies that a rejected reset does not erase
the saved preference. Source, converted and restored DB checks remain required.

A local full adapter build with Windows/UCRT could not execute because the
POSIX storage layer requires flock/fcntl/pread and related flags. No portability
stubs or relaxed build flags were added. Python syntax and whitespace checks
are local checks; socket and migration acceptance remain Hosted CI gates.

## Old-package converter restore regression (2026-11-20)

PR #134 merged as b7187d9938f9d8bbd238eaa9ce8dcea10f7a985e. Its native
FreeBSD/Rocky/Ubuntu tests, Linux compatibility, package jobs and Release Set
passed, but isolated upgrade run 44677431269 failed before installing the new
package. Authorized read-only inspection of that run's private failure logs
confirmed `Maintenance failed: no such table: article_format` at the old-package
`db-restore` call. Source helpers were new, while the explicitly selected installed
converter was old. The importer incorrectly audited every structured output as 5.

The importer now requires a known output schema, audits normalized relations only
for 5, and still reconstructs/hashes every stream and checks SQLite integrity/FKs
for all versions. The receipt records the actual output schema. This does not
silently upgrade an old converter or accept unknown formats.

The regression uses real C-generated schema 2/3/4/5 fixtures, substituting only
the external converter process. The original code reproduced the missing-table
error locally. After correction, all four valid outputs preserve 17 messages,
attachment bytes/order, UI settings and schema version. Hash mismatches, unknown
versions, missing attachments and missing schema-5 tables are rejected without
publishing the final DB or changing input streams (15 cases in total).
Local targeted suites: 2 structured-relation tests passed; 4 normalization tests
passed with 1 POSIX-only test deferred to CI. Windows directory fsync is not a
POSIX durability test. Actual old-package/jail acceptance remains required after
merge. No root helper, template, service or production data was changed.
