# Mail delivery, login notices and private transfer Core boundary

Baseline: main `cc0b1f65c0ede600bac49714ac4d61ac11db0846` (PR #94).
This grouped LN-23/24 change follows [mail resources](MAIL_RESOURCES.md).

## Contracts and authority

- `mail_core_sysop_send` selects the lowest numbered eligible configured
  administrator and commits a plain message before returning its ID and recipient.
  The caller cannot choose a different recipient, attach files, consume a draft
  or introduce a reply relationship through this API. Only this mail operation
  accepts an explicit guest actor `{0, 0}`; a null actor is never a guest.
- `mail_core_notice` returns owned unread-count and draft-revision values for the
  authenticated actor. It does not load message bodies, mark mail read, change
  preferences or modify drafts. A failed or disabled lookup returns no partial
  notice. Login remains successful when only the optional mail notice fails.
- `mail_core_transfer` accepts typed transfer requests, not a text command or
  caller-supplied capabilities. Each operation rechecks current identity, epoch,
  suspension, feature switch and member/moderation/mailbox health. Guest upload
  and download are denied even though guest SYSOP delivery is allowed.
- Transfer has two results: `MailOutcome` reports the authority/precondition
  check; `VaultResult` reports request validation and backend execution. Both must
  succeed before using the reply. Bounds are checked before backend dispatch;
  failure clears the entire reply, including partially populated metadata/data.
  Inputs and outputs must be distinct, correctly sized objects.
- The private `mail_files_execute` backend derives its own non-administrator
  principal. Existing owner, live-mail participant, reference, offset/retry and
  digest checks remain in `mail_files` and vault. Administrators acquire no
  private-file override. Withdrawal of credentials between chunks prevents the
  next request from using the former authenticated session.

Actors are supplied by the trusted board worker after session validation, not
by a remote user-provided identity field. The Core APIs independently enforce
authorization; caller UI state is insufficient. Their execution remains
synchronous and serialized in the board worker. No thread-safety or concurrent
repository access guarantee is added. Returned results contain no borrowed
repository pointers.

## Adapter and persistence compatibility

`board.c` owns raw command parsing, FILE name/description formatting, localized
errors, login notice publication and post-commit SYSOP delivery signaling.
`transfer_line_execute` parses the existing protocol and invokes a callback that
uses the authenticated MailActor. Its unused Principal argument grants no
capabilities. The special verbose FILE command retains its previous output.
Storage failures keep the existing `ERR no file`, `ERR vault storage`, and
`ERR mail storage or limit` vocabulary at their respective adapter boundaries.

Session rate limiting, composition/editing, login success, notification settings,
UI prompts, helper process lifecycle and transport buffering remain with their
existing adapters. This is not complete Session/Core or structured Web API
separation. Global mail repository startup/shutdown and feature configuration
also remain unchanged. No schema, migration, public command, HELP token,
transfer format or packaging dependency changes.

The public manual clarification covers already-existing SYSOP delivery isolation
from a saved personal draft. Its source fingerprints were reviewed against these
adapters and contracts, rather than merely updated to silence validation.

## Validation and pending CI

- Local MinGW GCC, C11, `-Wall -Wextra -Werror`: all 28 mail Core test groups
  passed. Six new groups cover guest/member delivery and administrator selection,
  commit failure, oversized input, notice failure without partial data, all eight
  transfer operations, malformed requests, backend failure output clearing and
  credential revocation before transfer completion. The existing 22 groups
  remain enabled.
- `mail_files.c` passed the same local syntax/warning check. The new socket
  regression is Python syntax-checked, not reported as runtime-tested locally.
- Canonical manual generation and validation passed: 42 canonical pages,
  2,812 local links, 172 legacy URLs, Markdown/HTML parity and offline ZIP.
- `tests/test_mail_delivery.py`, included in `make check`, exercises SQLite-backed
  guest/member SYSOP delivery, draft isolation, recipient changes after restart,
  private transfer retry/resume, suspension during transfer, administrator denial,
  legacy metadata formatting and disabled mail with public vault disabled.
- Existing Japanese/English notification, mail attachment, WebUI, transfer,
  storage and security regressions remain enabled. Full runtime and socket tests
  run in existing hosted CI. Native CI is restricted to the exact trusted main
  commit after merge; PR code is not run on home VMs or production.
- Full local Windows runtime compilation is not claimed: existing POSIX flock
  and lstat portability constraints are outside this change. No conditions are
  weakened and no separate test infrastructure is created.
