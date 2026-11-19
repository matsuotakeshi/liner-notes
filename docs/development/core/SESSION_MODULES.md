# Session adapter decomposition

Follow-up: [state and operation boundary](SESSION_STATE_API.md) supersedes the
flat Session layout and submission details below. This document records PR #127.

Baseline: main `663e7ec15bd7edb90e1c0846d79ffc0821b24166` (PR #126).
This change decomposes the complete former Session translation unit and the
textually included classic bindings into separately compiled adapter modules.
It does not finish multi-UI/SSH/Broker/DB work or claim that every RTBBS interaction
is UI-independent. Existing feature Core APIs remain responsible for business
operations and authorization; these modules adapt the current UI to them.

## Responsibility map

| Module | Ownership / responsibility |
| --- | --- |
| session.c | Public lifecycle, request identity/epoch attachment, incoming-line gate, timers, revocation and Session disposal |
| session_commands.c | Ordered named-command routing and utility fallback |
| session_results.c | Completion identity gate and ordered effect dispatch |
| session_output.c | Synchronous output, binary framing, pager, menus, prompts, HELP |
| session_parse.c | Legacy numeric, audit and FIND argument parsing |
| session_auth.c | Application/password dialogs, account commands, authentication effects |
| session_editor.c | Editor interaction, draft operations, imported/buffer/board editing effects |
| session_chat.c | Chat events, direct talk, auxiliary/status commands and completion effects |
| session_mail.c | Mail command/request adaptation and pending mail result handling |
| session_services.c | Gather/SYSOPMAIL and existing service command adaptation |
| session_board.c | Board management commands and entry/unlock continuation |
| session_members.c | Member directory, moderation and rights commands |
| session_reading.c | Article commands, read traversal, reports and profile continuation |
| session_settings.c | Preferences, menu/pager and guest handle settings |
| session_transfer.c | Transfer/batch commands, start authorization results, receipts and finish notices |
| session_vault.c | File listing/management and directory/edit/copy results |
| session_connector.c | Input normalization, connector envelopes and binary mode |
| session_extended.c | Existing extended command grammar leading to feature Core operations |
| session_menu.c | RTBBS screen input routing, numbered selections and Door return |
| session_classic.c | Classic menu bindings (formerly classic_commands.inc) |
| session_policy_core.c | Pure password-input decisions and transfer-outcome classification |

`SESSION_SOURCES` is the single explicit adapter build list used by both runtime
and test-adapters. Every module is compiled separately. No .c/.inc aggregation is
used to disguise the old translation unit. Source ownership is now functional,
while existing Core APIs (article, member, mail, vault, editor, etc.) remain shared.

## Public / private API and lifetime

The public `session.h` contract is unchanged. Runtime and transports call it;
`session_internal.h` is private to the adapters. `session_impl_*` functions are
collaboration entry points, not supported extension APIs. An int handler returns
1 when it consumed an input/result and 0 to continue. This is not an ACL decision.
The output helper copies bytes synchronously through SessionIO; failure aborts the
Session. No module retains a pointer to caller input or a BoardResult.

Session owns state, draft/undo buffers, credentials, protocol state and existing
Core value objects for its entire connection. One event loop mutates it. Modules
receive the same borrowed Session pointer; no additional threads, ownership copies
or OS handles are introduced. Submission copies the request into the worker queue.
The common submit path attaches session ID, slot, current user/credential epoch,
board and grants. Worker Core still rechecks permissions and persists the result.

The editor preprocessing scratch buffer belongs to the command dispatcher and
lives until every synchronous handler returns. Recursive named-command routing
gets another stack frame, as before. Secret input buffers and temporary credential
requests retain the original cleansing and throttling paths. Policy Core receives
only mode, byte length, cancellation and equality flags; it never receives secrets,
Session, user IDs, transport data or a storage handle.

## Ordering and compatibility

- Public line acceptance checks active/wait/pending/closing/transfer state before
  connector processing, normalization and menu routing. Secret/body text is not
  sent directly to the ordinary named-command table.
- Application input precedes editor preprocessing, password input and editor
  input. The named-command table retains legacy service/mail/extended/transfer/
  account/article precedence. Domain handlers preserve their original validation.
- `session_result` rejects inactive/closing or different session/slot completions
  BEFORE changing pending state or calling any effect handler. Cancellation,
  preference-exit, notification/revocation, traversal, lists, pending services,
  mail, chat, transfer, binary and ordinary results retain their previous order.
- In particular binary_enter while a transfer start is pending authorizes that
  transfer; it must not switch to binary-v1. The next standalone binary_enter can
  enter binary mode. Stale completions do neither.
- Unknown named commands still use the single ERR command fallback. Menus, HELP,
  locale text, number parsing, request op/action values, database formats, IDs,
  actor/epoch checks and public Session layout are unchanged.
- Pure password and transfer policies preserve legacy cancellation and outcome
  priorities, including RAW account /CANCEL being a short password error and
  menu account /CANCEL cancelling the prompt. This is not a password-policy fix.

## Validation

Local strict GCC C11 syntax checks cover every separately compiled adapter and
existing/extended test-adapters. New policy unit tests execute all password modes,
byte boundaries, cancellation/confirmation priority and all transfer receive modes
with protocol/recovery precedence. Python boundary checks enforce the single build
list, absence of textual source inclusion, private-header scope, centralized
board_submit and policy isolation from Session/IO.

A one-time token comparison before the policy extraction verified all 81 moved
legacy helpers (excluding the two intentionally decomposed dispatcher functions)
unchanged apart from linkage/helper names and compatible command parameter const.
Ordered command/result extraction was then reviewed against the baseline, including
transfer versus binary_enter boundaries and scratch-buffer lifetime. Token
comparison is review evidence, not a substitute for runtime regression.

The adapter regression adds RAW/menu and Japanese/English matrices for secret
cancellation, old/new/confirm mismatch, file-password confirmation, editor-versus-
command precedence, stale session/slot completion and transfer/binary precedence.
The catalog error-mapping scan follows all modules extracted from session.c,
preserving its former coverage; classic_commands.inc remains outside that legacy
scan as before. Local catalog/signature validation passed.
Existing adapter, Telnet, WebUI, chat, editor, mail, vault, Door and link suites remain
in the existing Hosted make check / workflow paths. No checks are disabled or
weakened and no unreviewed branch is sent to a home runner.

Local full adapter linking on Windows is blocked by pre-existing POSIX storage
APIs (flock/fcntl/pread/O_NOFOLLOW). The code is not patched around those dependencies;
actual adapter/runtime/package execution must be established by Hosted CI for the
candidate SHA. Native verification follows trusted-main integration only.

Canonical manual source mappings now point at the owning modules. Review confirmed
no new public command or setting: existing help/locale bytes remain unchanged.
Full-project modularization, independent UI selection, SSH and authentication Broker
are separate tasks. This PR's completion scope is the full Session adapter split,
not those subsequent features or a production rollout.
