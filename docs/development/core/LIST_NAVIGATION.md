# Listing continuation and directory navigation boundary

LN-23/24 development work based on main after PR #123. This groups report
continuation, interactive profile traversal and vault directory selection. It
adds no user command, permission, schema or public transport API.

## Core and adapter responsibilities

`list_navigation_core.c/.h` owns independent per-session values:

- `ReportNavigation`: report kind, target/ALL scope, next cursor and outstanding
  response flag. The Session no longer saves a command string as its report state.
- `ProfileWalk`: selected field/group/query and next member cursor, plus traversal
  flags. This replaces an entire saved `BoardRequest`, including its unused large
  body and credential fields, with a value smaller than 256 bytes.
- `VaultNavigation`: current/requested paths, the 16-row child listing and next
  cursor. A requested directory becomes current only after worker acceptance.

Core functions have no Session, UI, worker, storage, authentication or OS handles.
They belong to one event loop. Query snapshots own their strings; vault child/path
accessors return borrowed strings valid until the state is modified. Query/path
setter inputs must already satisfy the adapter's existing 80/48-byte bounds.

`list_navigation_protocol.c/.h` retains trusted worker marker parsing and renders
continuation commands from typed report state. Existing NEXT/TEXT NEXT precedence,
scanf valid-prefix behavior, root '-', indexed CHILD rows and response ordering
remain unchanged. These functions do not validate arbitrary remote client frames
or establish permissions. They are used after the usual worker completion checks.

Session parses commands, renders prompts and generates requests. Profile requests
are reconstructed with operation Z, action 10, one-result mode and the retained
field/group/query/cursor; `submit` attaches the current actor and credential epoch
just as before. Every page/selection still traverses existing Member/Vault/Core
and worker authorization. A remembered cursor, path or visible row is no grant.
Runtime calls `session_access_refresh` instead of inspecting saved command text.

## Preserved lifecycle and boundaries

- Report begin preserves the old cursor until a response. Clear/invalidation
  resets selected report/cursor, preserving the already outstanding request flag.
  A missing continuation marker clears report selection; a present malformed
  marker still means zero next, matching the old trusted-worker parser.
- BOARDACCESS invalidation clears only that report's page and offset. Other
  report/reader states and worker requests remain unchanged.
- Profile continuation retains field/group/query, updates a nonzero next cursor,
  stops at zero and clears private search bytes on authentication/revocation.
  Pending flags retain the existing queue-failure semantics; this is not a retry
  redesign. Session identity/slot checks precede all state updates.
- Vault requests preserve the current directory while pending or denied. An
  accepted board resets the same listing view/count and selected file as before.
  Parent navigation is read-only until its own board request succeeds. Child
  indices are 1..16; zero and out-of-range indices never index the array.
- Directory row parsing retains old trusted response semantics. No new behavior
  for sparse/out-of-order worker rows is specified. Reset also clears the old row
  storage, so inactive paths cannot survive an account transition internally.
- Duplicate vault-selection completion in the mail branch was unreachable after
  the first handler cleared its selecting flag; only the first handler remains.
- Repeated upload/resume, BATCH MATCH/COPY and menu commands still derive their
  paths from the same current-directory value. Direct root uploads are unchanged.

## Tests and evidence

Local Windows/GCC strict C11 tests passed: all seven report kinds and both scopes,
command buffer boundaries, marker priority/end/error cases, profile value-copy
and secure reset, isolated sessions, accepted/denied/repeated directory replies,
parent/root behavior, 16-child and 48-byte path boundaries. Session and adapter
strict syntax checks and Python test-harness compilation passed. An initial
conditional-expression rewrite and a signed enum assertion were corrected during
local compilation; no warning suppression or CI relaxation was used.

Adapter regression additions exercise Japanese/English report invalidation,
profile continuation and vault completion, stale identities, denial and revoke.
Existing Telnet `test_stage69_ui.py` now requires two matching profiles to be
traversed with the same field/query and checks directory retention after a failed
selection. These runtime executions await Hosted CI, not local syntax results.
`make check` includes the new Core/protocol test. Existing full tests, packages,
main-only native admission and CI scheduling remain in place.

Public manual clarifications cover existing continuation/root behavior only.
Canonical source mappings were reviewed against changed adapters; generated HTML,
Markdown, offline contents and links are checked separately. This development
record is not in the public manual allowlist. No production service/data or
release was touched.
