# Session state and operation boundary

Base: main bb652b441299778c6748693ecb5e7d63f856fcb6 (PR #127).
This follows SESSION_MODULES.md: compilation boundaries now also have explicit
state ownership and a UI-free operation submission contract.

## Ownership

| Owner | State |
| --- | --- |
| SessionCore | connection ID/slot, active/pending, clock/cooldown, authenticated actor/epoch, current board/article, board-password grants, login/guest identity |
| SessionAuth | password/application dialogue and sensitive temporary credentials |
| SessionPresentation | RTBBS Ui, paging, menu/classic bindings, screen geometry/preferences |
| SessionEditor | draft, undo, line editor and confirmation state |
| SessionReading | scan/check/report/profile continuation |
| SessionFiles | file selection, batch, category navigation |
| SessionMailState | mail input, navigation, attachment batch |
| SessionConversation | chat/talk/Gather/SYSOP-mail and deferred notifications |
| SessionConnection | output port, connector framing, Door/transfer state |

The event loop still owns all state. These are named structures, not anonymous
unions or flat compatibility aliases. Session adapters borrow only the lifetime
of the enclosing Session; no new threads or persistent pointers are introduced.
Secret buffers remain in SessionAuth, draft bodies in SessionEditor. Disposal
clears the complete aggregate and frees connector blocks before erasure. Existing
per-operation reset semantics remain intact; grouping does not reset unrelated
preferences, transfer receipts or notification state.

Session's C layout changes: rebuild all source consumers. There is no serialized
Session ABI or data conversion. Wire commands, database formats, IDs and installed
configuration stay unchanged. This is not a new external network API.

## UI-independent operations

Include session_core.h; neither Ui, Session nor a terminal is required.
The trusted owner initializes SessionCore.id/slot/active and advances its clock.
BoardRequest expresses the existing domain operation and its arguments; the
operation codes are the internal board worker contract, not terminal commands.

session_core_submit(core, request, port) validates active/pending/cooldown state,
replaces request session/slot/user/epoch/grants with the owner's authenticated
values, then calls port.enqueue. The callback must copy synchronously, must not
reenter this core, and must return whether it accepted the request. The worker
rechecks current identity and ACL on every operation. A caller cannot promote a
request by setting user, epoch or grant fields. The selected target board is an
operation argument; it grants no access.

Statuses: ACCEPTED, INACTIVE, BUSY, THROTTLED, UNAVAILABLE. Pending is set only on
acceptance. Accepted authentication attempts start the same one-second cooldown
for non-UI consumers as for legacy UI. UI modal/editor guards stay in the adapter;
a headless client has no editor dialogue to bypass. Each client owns its own core.

session_core_complete rejects stale session/slot and inactive/closing completion
before updating pending/clock. Only matching trusted worker results may then be
applied with session_core_identity. Auth success/failure resets actor/grants at
the same point as the legacy UI. Consumers must handle revoked results by ending
their session, and clear caller-owned credentials/bodies when no longer needed.
A client never manufactures BoardResult values or treats API pointers as network
credentials. The port is an in-process trusted dependency, not an authentication
service. BBS ACL still resides in domain Core/repositories.

Session uses this submission/completion boundary. Headless regression exercises
registration, article post/read/seen/list, private mail, denied guest requests and
restart using the same board worker, without constructing a Session or Ui.

## Structured effects and content

BoardFeedback is populated beside authorized producer outcomes. Preferences,
checkpoint commit, seen, pagination, vault board entry/unlock/menu/edit, copy,
batch matches, mail attachment IDs and child navigation use numeric fields.
Session no longer compares or parses completion.text to determine these effects.
Error-producing adapters set failed while preserving existing rendered messages.
Localized mail failures also carry failed: an active reader stops on operation
failure independent of whether the displayed prefix is ERR or MAIL ERROR.
Download/transfer adapters expose result status and continuation/created IDs;
their legacy text APIs remain available to existing callers.

Set SessionCore.structured for article/mail content projection. Authorized read
metadata is returned in BoardContent; body uses existing BoardResult.draft_body,
attachments use mail_attachments. No second full-body buffer is allocated.
Unsuccessful reads do not produce content.kind. Article withdrawn/concealed state
is the domain result; no hidden body is reconstructed. Default legacy article
reads continue erasing their scratch body as before. Vault guide editing also
uses the existing draft buffers; this is distinct from draft_loaded.

Display strings remain for existing RTBBS/RAW clients. Payload text can include
END NEXT or FILEBOARD UNLOCKED without creating a control effect. Numeric effects
and authorized structured content may be used without translating display text.
This is not a complete JSON/HTTP projection of every report row: the existing
WebUI still uses its RAW connector and presentation formatter. Adopting these
in-process APIs in a new Web/SSH transport is a separate integration step.

## Validation

- Local strict C11 checks: Session modules, adapter tests, headless test, vault,
  download and transfer adapters; pure Session Core/policy executables.
- UI-free unit tests: forged actor/grants overwritten, pending/queue failure,
  cooldown, inactive and stale completion rejection, identity change and guest ID.
- Adapter tests: existing RAW/menu and Japanese/English coverage, explicit typed
  fixtures, contradictory display markers versus trusted effects/cursors/grants.
- Existing Hosted make test invokes the real-store headless API test in a fresh
  temporary directory. No self-hosted PR execution or production access.
- Architecture checks reject reintroducing text-driven effects and UI dependencies
  into the core; catalog checks retain all former mappings.
- Local Windows runtime/board compilation remains limited by existing POSIX flock,
  lstat and Windows TZ warnings. No production build flags/tests are weakened.
  Hosted/native CI are required; syntax/unit success is not runtime success.

No MMM, SSH server, authentication Broker, DB migration or release is included.
