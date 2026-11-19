# RTBBS UI adapter and selection boundary

Base: PR #129, main `e7ea1053a4122b9dff181804415444988188a149`.
This is unreleased implementation documentation for Issue #54. WebUI and MMM
implementation are deliberately outside this change.

## Ownership and routing

`session_ui.h` defines a compiled, trusted `SessionUiAdapter`, not a runtime plugin
or an authorization boundary. The Session event-loop owner calls it synchronously.
The input line and result/view pointers are borrowed only for the callback. They
must not be retained, and callbacks must not directly modify Core identity/grants.

`session_init()` remains the compatible entry point and uses `rtbbs` with FIXED
policy. `session_init_ui()` accepts an administrator-owned registry/configuration,
validates it before producing output and copies its policy into the connection.
Adapter descriptors and names are immutable and must outlive that connection.
Failure leaves an inactive Session and returns zero; callers must not service it.

The registry rejects duplicate/invalid names, missing callbacks, unknown policy
values, invalid allowlist bits and an unavailable/disallowed default. There are
at most eight compiled profiles. RAW is a presentation/transport mode of RTBBS,
not a second UI profile. Only RTBBS is registered in the production default.

Input goes through the existing connector envelope/literal processing, then the
selected adapter. RTBBS alone performs classic uppercase conversion and key/command
mapping. It retains its existing command handlers, editor, HELP and language catalogs.
The helpers named `session_impl_command_core` are legacy RTBBS command translation;
the actual shared operation API is `session_core_submit`/`session_core_receive`.

Worker results are matched by connection/slot before the selected UI can cancel
navigation. Revocation is handled by Session, without delivering a normal UI result.
`session_core_receive` applies identity, epoch, grants and position exactly once
before the selected UI sees the typed `SessionResultView`. A navigation cancellation
cannot suppress authentication, grant changes or revocation. Stale results do not
reach either UI callback. Failed typed views expose no content/page/body pointers.
RTBBS still uses the trusted compatibility result to preserve existing localized
rendering and error dialogues; this is not a new externally accessible result API.

Prompt, menu, HELP, output formatting, input echo policy, timers and Door return
are routed through the active adapter. Binary framing and expiration stay outside
UI callbacks. Expiration is checked even during automatic reads. Secret input cannot
request uppercase echo. Door completion clears the Door state first and returns to
the selected adapter only while the Session is live; RTBBS retains its game menu.

## Selection API and limits of this change

`session_ui_select` supports FIXED, MEMBERS and ALL. It can select only a registered,
allowed profile; MEMBERS requires a Core-authenticated member. Administrator policy
is copied at connection creation. Later changes to the caller's config do not alter
an existing Session. The API does not grant authentication or change ACLs.

Selection is rejected while the connection is inactive/closing, waiting for a
worker or timer, taking a secret, running a Door/transfer, or consuming a connector
frame/block/literal. The outgoing adapter must also declare a safe main-menu state.
RTBBS rejects selection during login/registration dialogue, input/editor/confirmation,
paging/automatic reading/check traversal, mail/vault/batch, chat/talk/Gather and
submenus. A rejected or unchanged selection leaves state intact. RTBBS reentry
returns to its main menu without discarding the retained draft, settings or Core
identity/grants. No heap state is owned by the adapter descriptor.

The current Session still contains the named RTBBS interaction-state groups.
Future adapters must explicitly define their own state ownership and cleanup;
this registry does not make those RTBBS fields a universal UI schema. Connector,
notification, preference-save and revocation cleanup currently retain their existing
RTBBS-compatible state access. Those paths must be reviewed when adding another
production adapter, not inferred to support arbitrary plugins today.

No public UI-selection command, runtime configuration option, saved member UI
preference, preference fallback after login, MMM, MASH or WebUI migration is shipped
in this change. These remain integration work when adding a real second profile.
The current user-visible commands, HELP, storage schema and transport protocol remain
unchanged; the selection API is exercised with a test-only second adapter.

## Verification

`tests/test_session_ui.c` is linked into the existing `test-adapters` target. It
checks registry errors, defaults/allowlists, policy snapshots, guest/member selection,
blocked states, retained draft/settings/identity/grants, selected input/HELP/menu/
result/timer/Door routing, stale results, typed failures, cancelled position updates,
revocation and connector deadlines. It uses heap-owned Session/results to avoid
increasing the existing adapter test's stack requirement. Existing RAW/menu, Japanese/
English, authentication, editor, transfer and runtime regressions stay enabled.

Architecture checks enforce input dispatch and Core-before-UI completion; no UI
module may apply Core completions independently. Local Windows syntax checks and
pure Core executable tests supplement Hosted CI. Full POSIX adapter/runtime tests
are not claimed from Windows because existing storage code requires POSIX APIs.
The new behavioral adapter tests run in the existing Hosted Linux make check jobs;
trusted-main native/package CI remains unchanged. No home runner is used on this PR.

Canonical manual review: public commands/settings, protocol, HELP, Door behavior and
persistence descriptions remain valid. The protocol chapter clarifies that UI state
is not authentication and cannot override ACLs. New source ownership is mapped in
manifest/source-lock. Stable manual version is not advanced for an unreleased API.

Local pre-PR results (2026-11-19): all Session modules and adapter tests passed
C11 `-Wall -Wextra -Werror` syntax checks; Session Core executable tests passed;
eight architecture checks passed. Canonical build/ZIP/check passed for 42 pages,
2,814 internal links, 172 legacy URLs and Markdown/HTML/offline parity. Actual
POSIX adapter tests and package checks are pending Hosted CI for this PR SHA.
