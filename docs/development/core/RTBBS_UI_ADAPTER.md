# RTBBS UI / new Core integration

Base: PR #130 merged as `c31bd49e618053d73ed3cf2df4c39547ea8d2ffe`.
This document describes the unreleased RTBBS integration, not completion of all
of Issue #54. MMM, MASH, WebUI migration and subsequent UI implementations are
on hold at the user's request.

## Scope and completion map

| Boundary | RTBBS implementation |
| --- | --- |
| Input, prompts, menus, HELP, formatting | Registered adapter; existing RTBBS command/RAW/dialogue behavior |
| Operation submission | RTBBS context preparation, then `session_submit` / `session_core_submit` |
| Worker results | Session/slot validation and Core receive before RTBBS routing |
| Interaction state | Opaque, private per-profile `SessionRtbbsState` allocation |
| Notifications and chat callbacks | Current selected adapter, including delayed individual-talk events |
| Preference save/signature | Adapter callback; worker still validates account epoch |
| Reader stop/pause, board/access refresh | Adapter callbacks with existing navigation semantics |
| Transfer receipts/completion and connector input | RTBBS callbacks; shared transport framing/deadlines remain outside UI |
| Door return and geometry | Selected adapter receives return; dimensions belong to SessionConnection |
| Revocation, disconnect and identity change | Core invalidation plus secure UI state cleanup |
| Runtime status and service messages | Read-only snapshot / notice API; no RTBBS field access |

There is no remaining direct RTBBS interaction-state access in runtime.c, session.c
or session_ui.c. Source checks enforce this. All production UI operations continue
through the existing RTBBS command set, Core ACLs and stores. Typed result views are
available before rendering. RTBBS deliberately retains compatibility rendering of
trusted worker text alongside typed effects; removing its textual protocol is not
part of this refactor and is not required to call Core without a UI.

## State ownership and lifetime

`Session` contains Core, transport/transfer state and `SessionUiBinding`. It no
longer includes the RTBBS state header. A compiled adapter declares `state_size`;
the binding allocates zeroed private bytes only when that profile is first used.
RTBBS's password dialogue, editor, page buffer, search/navigation, mail, vault and
chat state are grouped in `SessionRtbbsState`, accessible only to its implementation
and focused white-box tests. Screen dimensions are shared transport values; no
profile owns the physical terminal size.

Callbacks and the worker queue run synchronously on the owning event loop. Input,
result and snapshot pointers must not be retained. Adapter descriptors/names are
immutable and outlive the Session; this is a compiled C contract, not an untrusted
plugin ABI or sandbox. The registry/config is copied per connection.

A successful switch lazily allocates the destination before leaving the current
profile. Each profile retains its own buffer and navigation/settings while inactive;
it does not borrow another profile's struct layout. Returning to RTBBS opens its
main menu while retaining its own buffer. A rejected switch leaves the active
profile, Core identity and state untouched. Allocation failure returns failure;
initial allocation failure closes the new connection before normal service begins.

Authentication results and successful logout discard inactive profiles so another
identity cannot inherit cached private content. Revocation invalidates Core first,
discards inactive state and calls active cleanup. Disconnect calls active cleanup;
inactive cleanup must not unregister the current profile's external callbacks.
Cleanup reason 0 means disconnect, 1 revoke, 2 inactive discard; only reason 1 may
request a revocation notice. Shared teardown cleanses every allocation before free
and is idempotent. RTBBS has no private heap children; any future adapter adding
such resources must release them in its cleanup callback.

## Operations, asynchronous events and security

`session_submit` accepts a typed OperationRequest without RTBBS parsing. Core
replaces actor, epoch, session/slot and grants before the worker rechecks ACLs.
RTBBS's `session_rtbbs_command` translates its existing text commands; it is not
Core. Its prepare_request callback sets navigation/preferences context, then uses
the same shared submission boundary. No duplicate business or authorization layer
is introduced.

`session_result` checks routing before invoking keep_position. Revocation never
enters normal UI result routing. Core applies auth, epoch, grants and position once;
a cancelled read can suppress navigation only. Failed/ignored typed views expose no
content pointers. The original trusted result remains available to RTBBS for its
compatible localized text/errors; it is not exposed as a new network API.

Runtime reads `SessionUiSnapshot` for menu/secret/edit/chat/automatic/redraw state.
It uses Core's admin flag for connection policy, never a menu flag as authority.
Runtime formats neither RTBBS prompts nor localized service text: notices and
announcements are rendered by the adapter. Secret/binary/Door/transfer state cannot
request chat-line redraw. No terminal input is echoed by a failed allocation.

Chat register/join install a common Session callback, which dispatches to the
currently selected UI. A callback retained by Chat Core cannot reinterpret another
profile's private bytes as RTBBS state. Inactive, closing, guest, transfer and Door
sessions are rejected at this common delivery boundary. RTBBS adds its existing
dialogue-specific checks. Mail notifications, account preferences, connector input,
transfer completion, reader refresh and Door return are likewise selected callbacks.
Transport deadlines and Core revocation are not delegated to optional UI behavior.

## Selection policy and intentionally deferred work

The internal API supports FIXED (default), MEMBERS and ALL, a compiled allowlist,
and a per-connection policy snapshot. MEMBERS requires Core authentication.
Registration rejects duplicate names, invalid policy/default/allowlist values,
zero state size and missing callbacks. Switching is rejected during pending/wait,
secret input, Door/transfer or partial connector input. RTBBS additionally requires
a safe main-menu state: no registration/editor/confirmation/paging/scan/check,
mail/vault/batch, chat/talk/Gather or submenu operation.

Production registers RTBBS only. RAW is a mode, not a second profile. The next
development candidate adds public UI selection/configuration, owner-only saved
preferences and login fallback (see [UI selection](UI_SELECTION.md)).
MMM/MASH/WebUI are not implemented or modified here.
The selection follow-up adds a settings table and UI commands; existing record
formats, IDs and the installation layout remain unchanged.
All C consumers must be rebuilt together; old struct layouts are not a binary ABI.

## Verification and evidence

PR #130 head `a9ffd6b3750fe45d07bdfdd90b9abe4271d2aebe` passed all 19 checks,
including [Linux compatibility](https://github.com/matsuotakeshi/liner-notes/actions/runs/44451226785),
before merging. That evidence is not a substitute for this follow-up SHA.

- `test_session_ui_lifecycle.c` links real Session/registry/Core with a fake worker
  port and two tiny UI states, with no RTBBS implementation/state header. It proves
  separate buffers, selected asynchronous delivery, secret/busy refusal, Core actor
  stamping, stale completion rejection, identity/logout invalidation, revoke and
  idempotent teardown. It is added to existing `make check`.
- `test_session_ui.c` retains registry/policy tests in full test-adapters and checks
  RTBBS private-buffer retention, JA/EN runtime notices, output bounds, secret redraw
  suppression and allocation teardown. Existing adapter assertions are retained;
  their white-box field paths now use the private-state accessor.
- Architecture checks reject RTBBS state/formatting in runtime and shared Session,
  direct Core result application in UI modules and old captured RTBBS chat callbacks.
- Existing Telnet, RAW, JA/EN, editor, mail, chat/talk, vault, Door, Web connector and
  real-store headless tests remain enabled in Hosted CI. No test conditions or CI
  trust boundary change. Native execution is trusted-main only.

Local Windows results: the new opaque-lifecycle executable and Session Core tests
passed; all Session modules and adapter tests passed C11 -Wall -Wextra -Werror syntax
checks; nine boundary checks and catalog/signature verification passed. Full runtime
compilation still hits existing Windows POSIX flock/TZ diagnostics. Actual POSIX
runtime/adapter and package outcomes must be checked in Hosted CI for this SHA.

Canonical manual review covers unchanged RTBBS commands, HELP, service notices,
transfer behavior, Core authorization and persistence. Source ownership/hashes are
updated after that review. Stable manual version is not advanced for an internal,
unreleased refactor. No release or production deployment is performed.
