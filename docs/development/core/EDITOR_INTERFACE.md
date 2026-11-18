# Editor command and presentation boundary (LN-23/24)

This unreleased refactor follows the document/mutation Editor Core and terminal
input separation. It extracts related editor adapters together, not a new UI or
an alternative publication path. MMM remains deferred.

## Ownership and dependency direction

- `editor_commands.c/.h` parses normalized slash draft commands and idle
  RTBBS line-editor commands into `DraftCommand` / `LineCommand`. It depends on
  Editor Core operation identifiers and existing numeric limits. Text/query
  pointers borrow terminated input; callers must finish using them before
  modifying/releasing that input. Parsing neither mutates a document nor grants
  permission to publish, save, attach files or access another member's draft.
- `editor_view.c/.h` renders draft listings, selected lines and search hits over
  validated Editor Core documents. It owns only per-hit temporary memory,
  released before return. Buffer results are complete or empty on failure.
  Search calls a synchronous output callback once per matching line, stops on
  write failure, and reports occurrence count. The callback must not change the
  document; its string is borrowed for that call only.
- Session still owns the draft and Undo buffers, editor mode, insertion cursor,
  receiving/confirmation prompts, timers, paging and language. It supplies the
  state to the parser, applies mutations through `editor_apply`, and uses the
  view results with the same emit/queue paths. The prior direct CRLF walks for
  line listing and replacement preview are replaced by Editor Core traversal.
- No global mutable state is introduced. A document is single-session owned and
  cannot be edited during a view call. Worker requests, authentication, ACL,
  saved drafts, common buffers and final article/mail publication remain on
  their existing Core paths.

## Compatibility details

Command case normalization still occurs in classic_input before parsing; payload
case is untouched. Exact slash words, whitespace, positive numeric limits,
open-ended ranges, the empty-list special case, search prompts and line position
clamping are preserved. /UNDO and /LINES still require exact names; unknown
slash strings follow the existing content/escape handling in Session.

The line command parser is called only after interactive input, slash commands,
`.` and `0` have been handled. Upload-clear prompts, 3-second text reception,
write confirmation and the special editor chat/online commands are unchanged.

Lists preserve numbering, framing and CRLF. Search remains literal,
case-sensitive, non-overlapping, counts occurrences, and emits the same yellow
ANSI markers when enabled. Match totals and Japanese/English prompts stay in
Session. The view layer never changes body/title/Undo. It checks the complete
CRLF document and requested range before output, and bounds every formatted
write; transport failure terminates search output instead of continuing work
for an already closed connection.

## Validation and limits

`make test-editor-interface` is included in existing `make check`: command grammar,
case/space/overflow rejection, range defaults and open ends, parsed mutation plus
Undo, byte-exact plain/ANSI listings and searches, prompted/empty queries,
UTF-8, read-only document/Undo snapshots, malformed documents, capacity and
callback failure. Maximum cases include a 256KiB document with 131,072 empty
lines and a single full-length line with a match at every byte.

`tests/test_adapters.c` adds Session routing checks for lowercase slash commands
with mixed-case text, Undo, ranged listing, prompted search, delete/replace and
return from write confirmation. Existing draft/editor/terminal protocol suites
remain unchanged and enabled in CI.

Local Windows checks cover direct interface and existing Editor Core tests,
strict Session/adapter syntax checks, i18n mapping and canonical manual. Runtime
Session integration is not claimed by syntax checks: it runs in hosted PR CI and
trusted-main native CI. No additional home-VM PR testing, deployment, release,
SSH or MMM implementation is part of this change.

## Native stack correction

Listing scratch storage is allocated only for listing and freed after output.
See [the exact native failure and follow-up validation](EDITOR_STACK_DIAGNOSTIC.md).
Hosted Linux regression uses the same 8MiB stack ceiling as native Linux.
