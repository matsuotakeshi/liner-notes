# Session state and operation boundary

Base: main bb652b441299778c6748693ecb5e7d63f856fcb6 (PR #127).
This follows SESSION_MODULES.md: compilation boundaries now also have explicit
state ownership and a UI-free operation submission contract.

## Ownership

| Owner | State |
| --- | --- |
| SessionCore | connection ID/slot, active/pending, clock/cooldown, authenticated actor/epoch, current board/article, board-password grants, login/guest identity |
| SessionAuth | password/application dialogue and sensitive temporary credentials |
| SessionPresentation | RTBBS Ui, paging, menu/classic bindings and preferences |
| SessionEditor | draft, undo, line editor and confirmation state |
| SessionReading | scan/check/report/profile continuation |
| SessionFiles | file selection, batch, category navigation |
| SessionMailState | mail input, navigation, attachment batch |
| SessionConversation | chat/talk/Gather/SYSOP-mail and deferred notifications |
| SessionConnection | output port, screen geometry, connector framing, Door/transfer state |

The event loop owns all state. The RTBBS groups now reside in a private per-profile
allocation; Session exposes only Core, connection and UI binding. These are named
structures, not anonymous
unions or flat compatibility aliases. Session adapters borrow only the lifetime
of the enclosing Session; no new threads are introduced. Private state is owned by the binding and must not
be retained after identity invalidation or disposal.
Secret buffers remain in SessionAuth, draft bodies in SessionEditor. Disposal
clears/frees private profile allocations and connector blocks before erasing Session. Existing
per-operation reset semantics remain intact; grouping does not reset unrelated
preferences, transfer receipts or notification state.

Session's C layout changes: rebuild all source consumers. There is no serialized
Session ABI or data conversion. Wire commands, database formats, IDs and installed
configuration stay unchanged. This is not a new external network API.

## UI-independent operations

Include session_core.h; neither Ui, Session, a worker lifecycle header nor a terminal is required.
See CORE_BOUNDARY_COMPLETION.md for the current receive/lifecycle contract.
The trusted owner initializes SessionCore.id/slot/active and advances its clock.
BoardRequest expresses the existing domain operation and its arguments; the
operation codes are the internal board worker contract, not terminal commands.
Bodies use the existing editor/storage representation: CRLF-delimited lines with
a final CRLF. The API does not silently normalize input. The real-store test
checks rejection of an unterminated mail body before sending its valid form.

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

session_core_receive rejects stale session/slot and inactive/closing completions
before updating state or returning any payload. Matching trusted worker results
apply identity, epoch, grants and position atomically; session_core_complete is
the compatibility entry point without a returned view. Auth success/failure resets actor/grants at
the same point as the legacy UI. Consumers must handle revoked results by ending
their session, and clear caller-owned credentials/bodies when no longer needed.
A client never manufactures OperationResult values or treats API pointers as network
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
The operation contract now also carries bounded typed pages from domain Core
(see CORE_BOUNDARY_COMPLETION.md). Existing WebUI still uses its RAW connector
and presentation formatter. A network JSON/HTTP schema and new transports are
separate integrations; the internal C structures must never be deserialized from
an untrusted network request or treated as credentials.

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

## PR #128 Linux test correction

At b955a7c750d98706051479c1e39cc24e27b12df6, Linux compatibility run
44439513837 failed on both Rocky 9 and AlmaLinux 9 at
 tests/test_session_operations.c:43 (mail_id assertion). The new headless test
submitted `private body` without the final CRLF required by mailbox.c:text_ok.
The production implementation correctly refused it. Correct the positive fixture,
retain an explicit rejection assertion for the malformed body, and make the guest
ACL-denial fixture syntactically valid as well. No production validation, timeout
or test assertions are weakened. Full corrected runtime validation is pending CI.

At 0b19a4136a7c71669efdf17658e5204e436813cf, Linux run 44441148867
passed the existing adapter tests but the new test timed out in exchange(). That
log did not identify the operation. Source review found that its same-process
board_stop()/board_start() sequence retained the worker's static ending flag;
the service lifecycle starts a new process instead. Do not infer a production
restart failure from that unsupported test lifecycle.

The harness now runs a seed process to register, post, send mail and test ACLs,
waits for its successful shutdown, then starts a separate verifier on the same
store. The seed reports its actual article/mail IDs; the verifier authenticates
again and checks both original IDs, titles and bodies. No globals or memory
indexes carry across restart. A failed seed or invalid ID manifest prevents the
verifier from starting. Either child's failure or timeout fails the test.

Each worker operation and start/stop boundary has a flushed diagnostic containing
phase, operation name and numeric result IDs. A request timeout additionally
identifies operation/action, session/slot and pending state. Credentials and
message bodies are never included. The existing 30-second operation deadline and
120-second child watchdog are retained. Harness unit tests cover failure and
timeout propagation; Linux compatibility runs the actual two-process store test.
Production code, data formats, validation and permissions are unchanged by this
test correction. The corrected runtime result must be tied to its own CI SHA.

PR #128 correction verification: commit 2bf4af75109bfbaa2d03cbf1e044624cdf78bbec
passed Linux compatibility run 44443314770 on Rocky 9 and AlmaLinux 9 and all
nine applicable Hosted workflows. Merged as a082f9484d0949bdd7953e1cd64631b06c40999d.
This evidence does not substitute for CI on subsequent boundary changes.

## RTBBS adapter follow-up

The registered UI receiver now calls `session_core_receive` before delivering a
typed view and the trusted compatibility result to RTBBS. See
[RTBBS UI adapter](RTBBS_UI_ADAPTER.md) for callback lifetime, selection policy,
safety barriers and remaining multi-UI integration work.
