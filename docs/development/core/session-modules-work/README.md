# Session decomposition work in progress — 2026-11-19

NOT BUILDABLE. Not merged, not a release candidate. This snapshot preserves work
when the operator's one autonomous repair/retest cycle limit was reached.

## Proven baseline

PR #126 was merged as `663e7ec15bd7edb90e1c0846d79ffc0821b24166`.
Its exact head `288d0d732b92012090198f134fe290eafb58761b` had 19 successful checks.
Pre-merge main `7ea34492427405968ee61cafb190dfedf14e697c` had all 10 workflows
successful, including Release Set and isolated FreeBSD upgrade acceptance.
Those results do NOT validate this work-in-progress snapshot or the merge SHA.

## Attempted scope

Replace the single Session translation unit and textually included classic menu
with independently compiled lifecycle, output, parsing, dispatch, auth dialog,
editor, conversation, mail, services, board, members, reading, settings, transfer,
vault, connector, classic menu and completion modules. Private collaboration is
through session_internal.h; public session.h remains unchanged. Request dispatch
and completion consumption are intended to retain legacy order and identity gates.

The extraction is incomplete. Additional Core decisions, boundary tests, actual
adapter execution, source/manual correspondence and CI have NOT been completed.
This must not be called the completion of LN-23/24 or the multi-UI architecture.

## Commands and results

Worktree: `phase1-session-modules-20261119` next to the prior form-flow worktree.
Branch: `refactor/session-modules-20261119`.

- `python validation-session/split.py`: produced the draft module decomposition.
- With `C:/msys64/ucrt64/bin` prepended to PATH:
  `gcc -std=c11 -Wall -Wextra -Werror -Isrc -fsyntax-only src/session*.c tests/test_adapters.c`
  failed. First attempt rewrote an include filename while renaming helpers.
- One correction restricted renaming to masked C identifiers, then the same compile
  check ran once. It still failed; see compile-errors.txt (latest full diagnostics).
- No runtime, full regression, package or manual validation was run on this WIP.
- No CI dispatch/push, native VM or production action was performed for this WIP.

## Remaining errors and restart plan

1. Identifier rewriting still changes struct members named classic_input and
   editor_input; it must rename function identifiers/references only, not member
   accesses. Check callbacks/function-pointer tables without changing strings.
2. Completion slicing searches a substring which also matches inside an indented
   block. binary_enter in transfer_result was wrongly selected as the start of
   binary_result. Use verified top-level statement boundaries, not substring-only
   offsets. Recheck all completion slices against the original source order.
3. menu_settings_command uses the old command_core local output buffer; give the
   owning handler an appropriate local buffer. Check other moved local lifetimes.
4. Audit routing fallthrough/handled results, all helper prototypes, labels and
   recursive command paths. The generation script is a disposable work aid, not a
   supported source generator. It reads HEAD and must NOT run on this WIP HEAD.
   To reproduce, use an independent copy at the baseline SHA above, or explicitly
   pin git-show inputs to that SHA; never overwrite later hand edits blindly.
5. After an explicit resume, fix these issues and run focused strict compilation,
   independently linked adapter tests and behavior/boundary regressions. Add the
   new tests to existing CI, not a duplicate environment. Complete responsibility
   and ownership documentation, update canonical source mappings and source-lock
   only after review (classic_commands.inc was removed in this WIP).
6. Commit a buildable candidate, push/open one grouped PR, then let required Hosted
   CI run. Never merge failing code or pass PR code to home native runners. Native
   CI remains trusted-main only. No release or production rollout is authorized.

The public manual is intentionally not updated to claim an unfinished feature.
