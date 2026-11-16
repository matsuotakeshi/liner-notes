# Saved article draft Core boundary

Baseline: main `f69324143b09c907ab6f586a4471b37cc66bb3b9` (PR #71).
This LN-23/24 slice extracts saved article copy status, save, resume and removal.
It does not change the wire protocol, editor commands, journal/SQLite format,
limits, mail drafts, login notices or the independent common editing buffer.

## API and ownership

The worker supplies its trusted ArticleActor (member ID, credential epoch and
board-password grants). Every call checks store health and the current member
identity; caller-supplied target-member IDs and admin flags are not accepted.
The API is internal and single-worker only, not a concurrent/network interface.

- `article_draft_status`: own saved revision; zero means no saved copy. A deleted
  copy's internal tombstone revision is not exposed as a resumable copy.
- `article_draft_resume`: owned board/parent/revision/title metadata and a copy
  of body into the caller's buffer. Rechecks read AND write ACL, password grants,
  parent existence/board and effective visibility, including hidden ancestors.
- `article_draft_save`: the same access checks, bounded title/body input, then
  the existing member store's exact revision comparison and durable append.
- `article_draft_remove`: exact revision deletion of the actor's own copy. Read
  and write ACL are deliberately not prerequisites for removal or status, so a
  member can still clean up a copy after losing board access.

No pointers are retained. Inputs and outputs must not overlap. Output metadata,
revision and supplied body storage are cleared on failure. Status returns no
title/body. Resume without a saved copy succeeds with empty output. A failed
body read now returns member-storage failure immediately, never DRAFT NONE.
The adapter selects/opens the editor only after successful nonempty resume.

Each call uses one MemberDraft scratch object; no persistent draft body cache is
added. The existing member store remains responsible for journal framing, text
validation, checksums, short-write detection and revision tombstones. Save keeps
empty bodies and the existing full draft capacity; post quotas, runtime body
limits and legacy board format requirements still apply at actual posting.
The Core never publishes an article, marks it read or deletes a saved copy as a
side effect of posting. Revisions survive removal and later saves as before.

## Adapter and compatibility

`board.c` translates d/w/o/X requests and typed results into the existing
DRAFT NONE/STORED/RESUMED/REMOVED and ERR replies. It retains session selection,
editor lifecycle and response rendering. Authentication's saved-copy notice is
still assembled in the login path; this change does not extract authentication.
Malformed control characters continue to be validated by members_store_draft,
with its existing failure response. Normal menu/Telnet/WebUI commands do not
gain a new syntax or permission, and shared-link imports are untouched.

## Validation

`tests/test_article_core.c` runs without UI, sockets or a live DB. Its saved-copy
suite covers per-member ownership (including admin non-override), exact/stale
revisions, concurrent-editor conflicts, deletion tombstones, empty/max bodies,
read/write/password revocation, hidden/missing/wrong-board parents, revoked
identities, broken stores, partial reads, failed writes, too-small output buffers
and clearing all error outputs. Existing Core suites run with this addition.

The existing `tests/test_saved_draft.py` remains in make check / hosted CI. It
covers real sockets, UTF-8, concurrent sessions, restart persistence, ACL loss,
hidden parent, empty/max legacy draft, Japanese/English Telnet and actual short
writes/corrupt journal rejection. Do not treat local mocked-store tests as this
integration test or as native-OS evidence. Full runtime checks are delegated to
the existing PR CI; trusted-main native CI follows merge. No production changes.

Reviewed docs/manual/editor.md (saved drafts) and help/ja/draft.txt plus
help/en/draft.txt against the extracted behavior; their public instructions
remain correct and unchanged. Only reviewed implementation digests are updated
in source-lock.json. Canonical HTML, internal links, search and offline parity
are checked using the existing tooling. This development record is not published
as a new public feature page.
