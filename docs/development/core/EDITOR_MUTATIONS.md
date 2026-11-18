# Session-owned editor Core

Base: `84a78aba69e419cde2ba0edfe836cb8dc8604e85` (PR #108 merge).
This LN-23/24 increment groups document mutations, single-level Undo, line/range
inspection and literal search for the existing article, mail and document editors.
It is not a new UI, persistence format or publication API. MMM remains deferred.

## Responsibilities and ownership

- `editor_core.c/.h` has no Session, BoardRequest, UI, socket, database or member
  dependency. It accepts a borrowed view of the session's title/body/Undo storage.
  It provides typed outcomes and byte spans rather than commands or messages.
- `session.c` retains RTBBS/slash command parsing, line cursor and input modes,
  idle upload timer, confirmation, language, prompts, line numbers, highlighting,
  attachment commands, preview formatting and transfer to storage workers.
- Authentication, ACL, suspension/epoch and validation at publication still belong
  to Article/Mail/Member Core and their repositories. Editing a document grants no
  permission to publish it or access someone else's saved buffer/draft.

The five memory regions in EditorDocument are session-owned and disjoint. Arrays
have fixed documented capacities (81-byte titles and CONTENT_BODY+1 bodies).
Borrowed text arguments must not alias them. Calls are synchronous on the session
event-loop owner; no cross-thread access or global mutable editor state exists.
Spans point into the current document and expire on mutation. Iterator offsets
and lengths are byte values from a prior successful inspection; do not mutate
the document while iterating. UI callers decide how to render these values.

## Preserved editing behavior

- Insert/replace/remove, multi-line deletion and quoting, append line, import
  block, title change, clear and Undo share one implementation.
- Title and identical replacement no-ops preserve the previous Undo. Failed
  bounds/control/capacity checks leave document and Undo unchanged. Successful
  clear/import still create an Undo snapshot, even when empty, as before.
- Undo restores both title and body once, then cleanses its snapshot. It is not
  persisted or shared with another connection. Cancel, logout, successful finish
  and a newly loaded draft retain their existing snapshot-clearing behavior.
- Quoting reuses the Undo copy as its immutable source, avoiding another full
  body temporary. Deleted/cleared trailing bytes are cleansed after mutation.
- Body capacity remains 256KiB including CRLF. Lower configured publication
  limits are still enforced by existing storage/Core paths, not silently moved
  into a new per-session setting. Input-line limits remain transport-owned.
- Search is literal, case-sensitive and nonoverlapping within each selected
  line; matches do not span CRLF. Match offsets/sizes are UTF-8 bytes. Session
  retains the existing Japanese/English counts and optional ANSI highlighting.
- The old adapter's line grammar, acknowledgements, no-output range operations,
  confirmation and cursor advancement on successful insertion are retained.

Inspection explicitly rejects unterminated/invalid CRLF buffers rather than
letting legacy pointer traversal dereference a missing line ending. This is a
defensive boundary for malformed input, not a converter for legacy data.
The Core validates borrowed-buffer shape but cannot validate caller allocation
sizes; callers must obey the header's ownership/capacity contract.

## Verification

`make test-editor-core` (included in check) covers six groups: mutation/Undo,
title/no-op/clear, quote/import, exact body capacity and maximum blank-line count,
literal/UTF-8 search spans, malformed input and independent sessions. Direct
failure cases compare all document and Undo bytes to the pre-call fixture.

`python3 tests/test_editor_core.py ./runtime` is included in the existing socket
suite. It exercises article and mail editing, scoped search, quote/delete/Undo,
other-session isolation, persisted drafts and restart in journal and authoritative
SQLite modes. Existing Telnet, connector/WebUI literal-input, line editor, preview,
saved draft, private mail and shared-buffer tests remain enabled.

Local Windows UCRT direct tests (six groups), strict syntax checks and Python
compilation passed. These are not native runtime results. The existing hosted CI
executes real-socket/full regression; native machines continue to run trusted
main only. Record results against the actual tested commit, not an older SHA.
