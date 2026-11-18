# Mail navigation and draft-response boundary

Development work for LN-23/24, based on main after PR #122. No new mail commands,
schema, public API, ACL exceptions or release are introduced.

## Responsibilities

`mail_navigation_core.c/.h` owns per-session listing selection, query bytes,
continuation position, selected message, reading flag and deletion confirmation.
It returns owned listing plans and uses Traversal Core for reader positions.
The shared `MailListMode` definition lives in `mail_query.h`; its existing numeric
worker representation is unchanged. No UI, socket, storage handle, member object
or worker request is retained by this value-only module. Calls belong to a single
Session event loop; independent sessions have independent state.

Session parses text commands and dates, maps worker actions to semantic events,
builds requests, and renders Japanese/English/RAW responses. `classic_commands.inc`
still selects RTBBS keys. WebUI's existing connector state fields and wire format
are unchanged. All resulting operations still go through the worker and Mail
Core, which recheck the member epoch, feature state and participant ownership.
Selection, a listing plan, and a confirmed ID are not authorization credentials.

The Core also returns a draft effect from a validated response: open, retain,
save complete, send complete, retry editing or replace attachments. Session keeps
ownership of the text, attachment data, Undo state and secure erasure. Revision
validation remains in worker Mail Core; this extraction does not change it.

## Deliberately retained behavior

- SEARCH/NEWS and their FROM variants retain the exclusive listing cursor and
  query across direct reads and rejected requests. Plain lists restart at zero.
  Invalid query selection leaves all state intact. Query plans own their bytes.
- A successful selected ID updates the current mail even outside `mail_pending`.
  LIST/FILES leave reading mode. The next listing cursor changes only inside the
  awaited mail-response branch, not during unrelated completion processing.
- FILES keeps using the existing listing cursor; this change does not redesign
  that interaction. Non-search listing plans never carry a retained search term.
- A failed read or deletion does not invent a new selected message. Confirmation
  retains the requested ID even if the current selection changes. Either answer
  consumes it, including when a later submission fails. Worker authorization is
  still required for confirmed deletion.
- ASC/DESC, repeat, previous and relative movement retain Traversal Core's ID
  boundaries. Existing menu/raw differences and scan filters remain intact.
- Only nonzero recipient opens/resumes a draft; only nonzero saved revision
  clears a saved draft. A failed send retains the body and reopens editing.
  Successful send clears text but preserves the existing revision/Undo behavior.
- Authentication/logout clear listing filter, query and cursor as before; they
  do not add a new selection reset. Revocation consumes deletion confirmation.
  Session disposal still cleanses the complete object.
- Session/slot validation and cancelled-sequence handling precede observation.
  The duplicate, idempotent mail observation inside the awaited-response branch
  was removed. Unrelated vault response handling was not changed.

## Verification

Local Windows/GCC: direct Core tests passed for all list modes, UTF-8 and 80-byte
query boundaries, rejected queries, copied plans, per-session isolation, cursor
ownership, confirmation consumption, ID boundary movement and 80 draft-response
combinations. Strict C11 `-Wall -Wextra -Werror` syntax checks passed for Session,
Mail Core and the adapter test. The local Python harness compiles successfully.

`tests/test_adapters.c` adds Japanese/English reply mapping, stale session/slot
rejection, list/read/attachment-list transitions, draft failure/success and
confirmation revocation. `tests/test_mail_reading.py` adds actual socket checks
for retained paging after direct/denied reads and invalid search requests, plus
cancelled deletion. These execution tests are pending GitHub-hosted CI; syntax
checking is not a claim that they ran locally. Existing mail privacy, drafts,
attachments, menu and WebUI regressions remain enabled.

`make check` now includes `test-mail-navigation-core`. No workflow, native-host
admission, release gate, package configuration or timeout was changed. Native
execution belongs to trusted main after review and merge. No production host,
user data or service was accessed.

The public mail chapter clarifies existing list-reset behavior. Source mappings
were reviewed for mail, article search, editor, connector, files, accounts,
permissions and build instructions: remaining differences are internal field
ownership only. Canonical manual generation/link/offline parity is validated
separately; development notes are not added to the public publication allowlist.
