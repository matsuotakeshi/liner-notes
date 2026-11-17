# Mail composition and lifecycle Core boundary

Baseline: main `5973b2feb08f5a70310ae4658b80919622ef50c8` (PR #90).
This grouped LN-23/24 extraction covers recipient/reply preparation, draft
metadata/resume/save/removal, sending, participant deletion, attachment selection
and unused attachment removal. It does not extract reading, listing, searching,
transfer protocols, guest SYSOPMAIL, authentication or UI editor control.

## Ownership and authority

`mail_core.h` defines MailActor, typed outcomes, owned compose/draft results and
borrowed write input. The worker supplies the authenticated ID and credential
epoch. Every entry point independently checks member/moderation health, current
identity, suspension (including pending/retired states), epoch, configured mail
availability and mailbox health. No API accepts an administrator override or a
caller-selected draft owner. These APIs are synchronous, single-worker-only;
no input/output overlap or retained pointers is allowed.

`mail_repository_enabled` is the existing board owner's policy bridge. The
existing mailbox and mail_files modules remain the persistence and attachment
boundary. No new connection, schema, background writer or file format is added.

- Preparation resolves the exact login through the existing member-name index,
  replacing the adapter's full member scan. A reply requires an incoming message
  for this actor, and uses its sender/ID and optional inherited title. It does not
  mark the original read. Invalid/suspended recipients remain unavailable.
- Draft results own revision, recipient name/ID, parent, title and attachments.
  Metadata-only access avoids loading the body and remains available for cleanup
  when a recipient is suspended or the reply parent is deleted. Resume rechecks
  both and copies the body to caller-owned storage. A local copy of MailMessage
  prevents subsequent mailbox calls from invalidating borrowed cache contents.
- Save preserves the store's validation and exact expected revision. As before,
  saving a draft does not impose the send/resume suspension check on its recipient;
  send/resume still refuse unavailable recipients. The existing store owns reply
  validation, text framing, duplicate/invalid attachment IDs and revision handling.
- Sending revalidates recipient eligibility and attachment ownership. Atomic send
  plus draft consumption remains a single existing journal operation. Only a
  committed result returns a delivery ID for the adapter to notify.
- Deletion requires participant access, with no SYSOP override, and preserves the
  existing rule that deletion removes the mail for both parties.
- Selection checks ownership on add (including duplicate adds), keeps the 16-file
  bound, and permits detaching/clearing stale selections. Save/send recheck every
  attachment. Drop delegates existing owner/reference checks to mail_files.

## Failures, limits and completion

All failed owned-result operations clear their outputs; failed persistence cannot
publish a nonzero revision/delivery ID. Resume also clears its body buffer after
partial read failure. Current body quotas apply to new writes; previously saved
content remains readable/resumable even if a smaller quota is configured.

Cleanup runs only after a successful durable save/send/delete, preserving the
existing best-effort attachment reclamation behavior. A deferred physical reclaim
is not represented as a failed logical mail commit; mail_files retains its own
retry/reporting behavior. This extraction does not redesign reclamation.

board.c retains language selection, protocol formatting, session fields, attachment
description, notice publication and the existing generic storage error path after
a failed body read. No new menu, command, translation key or wire format is added.

## Verification

Local C11 warnings-as-errors tests pass 12 grouped Mail Core checks. The fixture
calls Core directly without UI guards, covering all eight entry points, guest and
revoked identities, pending/retired/suspended actors, store/feature failures,
private replies, administrator non-override, cache-independent drafts, partial
body failures, buffer bounds, revision conflicts, send consumption, attachment
ownership/full selections, failed-write cleanup ordering and body/title bounds.
The persistence fixtures are stubs, not evidence of a live OS or database test.

`tests/test_mail_service.py` adds real-socket regression to the existing test
suite: recipient suspension between edit and send, retained drafts after failure,
restart with mail disabled, lower body quota, successful retry, private replies,
deleted-parent cleanup. Python syntax is checked locally. Runtime execution is
pending hosted CI and then exact-main native CI, not claimed as a local pass.
Existing mail, draft, attachment, Japanese/English menu, WebUI and storage-fault
suites stay enabled. No full local/native suite is duplicated.

Public mail guidance and Japanese/English HELP clarify existing ownership and
revision behavior. Canonical generation, links/search and offline parity are
checked before PR publication. Stable manual version remains 0.74.1; this internal
refactor is unreleased and does not complete all Core/UI separation.

## PR build correction

PR head ef3c5e41834138332291aae625ff0747398e94d9 failed POSIX builds:
Native CI Source Checks job 134037267213 and Rocky Linux compatibility job
134037279010 both report an undeclared setenv from runtime_limits.h. The new
translation unit omitted the POSIX feature-test macro used by board.c. Windows
unit checks did not exercise this declaration because the header uses _putenv_s
on Windows. Declare _POSIX_C_SOURCE=200809L before every include in mail_core.c;
retain strict C11/warnings-as-errors and all existing tests. Mail behavior, quotas
and environment loading are unchanged. The correction's local targeted checks
are supplemental; POSIX/runtime results await the new PR CI commit, not the old
successful main commit. No CI rerun of the known-bad SHA is requested.

## Quota refusal completion correction

At head 623e05ac03b664c0171a4a0e048175faa73afaec the POSIX builds passed;
Rocky/Alma Linux compatibility jobs 134048246236 and 134048246184 then failed
in test_mail_service.py on /SAVE after an over-quota send. mail_execute returned
ERR body limit before setting mail_action. The session stops editing while send
is pending and restores it only for an S completion without mail_id. Consequently
the quota error failed to restore editing, unlike the other rejected send paths.
The same early-return ordering existed before the Core extraction.

Set the completion action before the quota guard. Keep quota rejection and the
existing assertions unchanged; capture actual socket responses in assertion
messages. The test still checks failed send AND save, cancellation, unchanged
durable revision, restart and eventual delivery of the retained body.

A local C11/warnings-as-errors harness compiled the actual mail_execute prefix
from before/after sources: old returned 3 for missing completion kind; corrected
code returned 0 for both S/W quota rejection and normal paths. Files and result
are under build/mail-quota-reproduction/. This isolated contract reproduction
is not a full runtime/socket pass. Socket-test syntax and diff checks passed;
new hosted CI must execute the full regression. No test was disabled or relaxed.
