# Schema 4: ordered mail attachment relationships (candidate)

Based on main 449fceca. This is one normalization step, not completion of LN-28.

## Storage and compatibility

New structured databases use schema 4. Existing schema 1/2/3 databases keep their
version and write codec. Opening an existing database does not migrate it.
`mail_attachments(stream, offset, ordinal, file_id)` references the owning mail
journal row with ON DELETE CASCADE, has a 0..15 ordinal, positive integer file ID,
and unique file IDs within each event. The `(stream,offset)` key deliberately
identifies a historical event; a message ID can have several events. File IDs are
not foreign keys to the vault: attachments have their own namespace and payloads
remain external. IDs and historical ordering are preserved.

Schema 4 mail attributes contain only the 44-byte compatibility header and the
4-byte checksum (48 bytes). Attachment IDs are not duplicated in that blob.
The bounded C codec and Python exporter reconstruct the original logical record
on demand. Missing/extra/gapped relationships fail closed. Count and original
checksum remain compatibility metadata, checked by the existing mailbox reader.
Parent row, attachment rows and stream length/revision are committed in the same
existing transaction. Failed inserts roll back through storage_sync.

## Migration and recovery

Use the existing offline `db-structure` to create a separate copy. Keep the source
and backup. The source must be stopped; the exclusive lock is still enforced.
The converter streams records into a new schema-4 database; every logical stream
is SHA-256 compared with its original. integrity_check and foreign_key_check must
both succeed. Failure leaves the copy incomplete; do not start that copy.
Schema-4 backups retain structured storage; restore reconstructs a new schema-4
copy. Old schema-2/3 structured backups also reconstruct into current schema 4.
Keep the old executable and original DB together for rollback; older executables
reject schema 4. Do not change storage_meta to bypass that rejection.

## Coverage and remaining normalization

Already typed: main article/mail/member fields and 13 history streams; settings
are relational tables. Mail attachment lists are now relational and indexed.
Still retained: member ancillary event envelopes, article format/checksum
metadata, unsupported streams such as vault metadata in blocks, and separate
link/Web/Door databases. These require their own compatibility and recovery
review; this PR does not merge namespaces or remove historical records.

## Tests

- C codec: schema 3/4, 0..16 attachments, byte-identical reconstruction,
  relationship constraints/cascade, failed transaction and corruption rejection.
- Python: export real C-produced databases; schema 3/4 bytes/hashes match;
  missing attachment rejected.
- Existing socket migration test extended with upload/send, three attachments,
  copy migration, foreign-key check, actual payload reads, restart, backup/restore,
  subsequent monotonic mail IDs and source preservation.
- Linux/native runtime, crash/maximum body/scale/package tests remain CI gates.
  Windows local codec/export success is not evidence of those runtime results.
