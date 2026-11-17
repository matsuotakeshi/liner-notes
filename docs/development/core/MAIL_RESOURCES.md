# Mail attachment views and common-buffer Core boundary

Baseline: main `16b5d2319a37d311bc5a2e0957487602cc41d0f0` (PR #92).
This grouped LN-23/24 change extends Mail Core with attachment listing and
metadata, message attachment batch selection, and common-buffer import.

## Responsibility and ownership

- `mail_core_files` accepts an actor and inclusive upload cursor. It returns an
  owned `MailFilePage` of up to eight uploads and a continuation ID. Only uploads
  belonging to that actor are listed, including active reservations. Cancelled or
  reclaimed entries are omitted; committed names and the active `-` name retain
  their prior representation. This cursor is distinct from MailQuery's exclusive
  message cursor. No page-sized result contains borrowed store pointers.
- `mail_core_attachment_info` returns owned metadata for an owner or a live mail
  participant. Receiving a file does not make it appear in the receiver's own
  upload list or grant the ability to attach it to a new message. Existing
  `mail_files`/vault participant and reference checks remain the store boundary.
- `mail_core_attachment_batch` returns up to 16 IDs in the message's stored
  order. It does not read a body, mark mail read, or grant transfer authority.
  A missing blob is still handled by the existing per-file transfer execution;
  it does not silently disappear from the selected batch. Transfer rechecks
  access when executed. No payload, resume or individual-completion behavior changes.
- `mail_core_import` checks message participation and reads a local copy of its
  metadata before loading the body. It synchronously writes only the actor's
  common buffer through `mail_repository_buffer_save`. The repository consumes
  the borrowed body before returning and must not invalidate the mailbox cache.
  Mail read flags, messages, drafts and attachment references are not modified.

All four APIs independently check member-store/moderation health, current actor,
credential epoch, suspension, feature switch and mailbox health. A UI flag never
grants private-mail access; administrators have no override. Failure outputs are
cleared even when the underlying metadata provider dirties its output before
returning failure. Invalid counts/names are rejected before rendering. An upload
store error while building a page now fails the whole page rather than presenting
a partial listing as complete. Common-buffer persistence failure is distinguished
from mail-body load failure; neither produces a success completion.

## Adapter and persistence compatibility

`mail_files.c` now exposes typed metadata instead of localized text. board.c owns
the existing raw/Japanese/English MAIL FILES, attachment description and error
formatting. `features64.inc` maps GET and batch commands to Core results and keeps
their existing successful completion strings and feature-disabled replies.
No new public command, database schema, migration, transport or worker is added.
Work remains serialized in the existing board worker; no API is reentrant.

Public manual clarifications describe already-existing owner-list pagination and
non-marking common-buffer imports. HELP/localized strings and protocol tokens
are unchanged. [The following delivery extraction](MAIL_DELIVERY.md) moves guest
SYSOPMAIL, login notices and private transfer execution into Core, with raw
transfer parsing in the adapter. Neither change claims complete mail/Core or
Web API separation.

## Validation and limits

- Local MinGW GCC C11 `-Wall -Wextra -Werror`: all 22 `test_mail_core.c` groups
  passed, including four new resource groups. Covers old composition/read APIs,
  owner/participant versus administrator access, all actor/store/feature gates,
  partial outputs, 16-ID batch order, deleted/empty messages, unread preservation,
  body load and buffer write failures, drafts and maximum-sized buffer import.
- `mail_files.c` passes the same local syntax/warning check.
- Full board.c syntax checking on MinGW encounters existing POSIX `struct flock`
  and `lstat` declarations. These unrelated portability constraints are not changed
  or suppressed. Full runtime build/execution is delegated to existing hosted CI.
- `tests/test_mail_resources.py` is syntax-checked locally and added to `make
  check`: SQLite-backed socket tests for more than eight owned uploads with ID
  holes, active/cancelled uploads, drafts, recipient visibility, administrator
  denial, common-buffer import, restart and reference reclamation. Its runtime
  result is pending CI, not asserted from syntax checking.
- Existing attachment Japanese/English menu, mail transfer protocols, batch,
  WebUI and storage regressions remain enabled. PR checks are hosted; exact-main
  native CI follows merge. No PR is run on home VMs or production hosts, and no
  duplicate local full regression is run.

## Initial-page test correction

PR #94 head `183975cd1cee311c5d3ddb0123fc6f856d8fa79d` failed in both
Rocky 9 and AlmaLinux 9 at the new socket test's first attachment-list header
([Linux compatibility run](https://github.com/matsuotakeshi/liner-notes/actions/runs/44111974797),
jobs `134139860319` and `134139860475`). The test sent `MAIL FILES 0`.
The existing `session.c` number_arg rejects zero: the initial text request is
`MAIL FILES`; only continuation requests include the positive next ID. This is
distinct from the Core API's initial zero cursor. Production parsing is unchanged.

The helper now omits the initial cursor and retains exact header assertions,
including the request and actual header in a failure report. Local scripted-client
checks cover two-page continuation, an empty list and rejection of an error
header. Python syntax and diff checks pass. Real socket verification remains
pending the corrected PR CI; prior successes do not validate the new SHA.
