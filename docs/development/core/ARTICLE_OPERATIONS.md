# Core boundary: article withdrawal and retitle

Baseline: main `34248a08d42cba8d16ca467f536cecaa709433d1`.
This is the first LN-23/24 extraction, not completion of the Core/UI refactor,
Issue #54, Door authentication, or SSH. There is no new public command or wire
API. The canonical manual and Japanese/English HELP keep their current behavior.

## Existing responsibilities and dependencies

| Area | Current responsibility | Remaining coupling |
|---|---|---|
| `runtime.c`, `terminal.c`, `telnet.c` | Connection lifecycle, bytes, timers, Telnet negotiation, PTY/transfer transport | Runtime drives Session and dispatches worker results |
| `session.c`, `classic_commands.inc` | Menus, command parsing, editor, session presentation state | Builds legacy BoardRequest operations; consumes formatted results |
| `ui.c`, `messages.c`, `menu.c` | Terminal formatting, languages, external HELP/menu text | Not an authority source |
| `board.c` | Single-owner worker/queue, global store health, legacy request/response adapter, article repository | Many other operations still mix formatting with domain logic |
| `article_core.c` | Typed withdrawal/retitle, reads and local hide/restore; live identity/ACL checks, durable mutation order | Uses existing member/catalog/moderation/extras modules on the same worker |
| `members.c`, `catalog.c`, `moderation.c`, `board_policy.c` | Membership, epochs, ACLs, inherited SIGOP roles, board grants | Existing process-global worker-owned state |
| `mailbox.c`, `mail_files.c`, `chat.c`, vault modules | Mail, attachments, chat/talk, vault services | Their Session/Board command adapters remain unchanged |
| `doors.c`, `board_link.inc`, `webui/bridge.py` | PTY Door management, signed article exchange, Web connector | Web still uses the legacy command bridge; no new auth transport |
| `storage_sqlite.c`, `db_index.h`, `extras.c` | Storage streams, article index, durable audit journal | No schema, persistence order, or data migration change |

The extracted direction is legacy request adapter -> typed article Core ->
identity/ACL modules and repository. Core includes neither `session.h`, `ui.h`
nor `board.h`, and produces no terminal text. The internal repository interface
does not accept UI-supplied ownership/visibility/provenance flags.

## API and ownership

`article_withdraw(actor, board, article, confirmed)` and
`article_retitle(actor, board, article, title)` return `ArticleOutcome`.
`ArticleActor` is built by the trusted worker adapter from authenticated session
identity and its epoch/grants. It is **not** a credential, a JSON schema or a
client assertion. Future transports must authenticate through Core and use the
worker queue; exposing this C structure over the network is not safe.

Both functions must run synchronously on the existing single board worker.
They are not thread-safe or reentrant. No additional mutex or worker is created.
The queue retains its existing request copies, ticket/slot/session routing and
stale-completion handling. The actor, title and grants are borrowed only until
return; the policy grant context is cleared on every return. Repository lookup
copies metadata so no transient SQLite index pointer escapes. No article body
is read or retained by the mutation operations; structured reads are described
separately in [Structured article reads](ARTICLE_READ.md).

Core checks storage health, member existence, suspension and current epoch;
then board membership/read ACL, owner or current board manager. A manager still
needs read access. The existing catalog resolves parent SIGOP inheritance.
Confirmation is not an authorization lease: the execution call rechecks identity,
board password revision and ACLs. Existing board-wide health/feature guards
remain in the worker; this is not yet an independent embeddable BBS service.

Withdrawal rejects imported-origin articles, including for local administrators,
before returning confirmation. It commits the same extras `D` record and actor.
Retitle also checks effective ancestor visibility, write ACL and the existing
1..80-byte/no-control title constraint. It commits extras `T` before publishing
the same title/index update. Persistence failure never reports success or calls
the title publication hook. Existing journal idempotence, audit order, article
IDs, link signatures and visibility cache invalidation remain unchanged.

`board.c` maps outcomes to the unchanged RAW/terminal responses. The existing
Session/menu confirmation and Web connector therefore need no new protocol.
Local HIDE/RESTORE are now extracted as described in
[Local article moderation](ARTICLE_MODERATION.md). Search, ancestor visibility,
link synchronization and physical body reclamation remain in existing modules.

## Verification and limits

- `make test-article-core`: direct typed API tests without Session, UI, sockets
  or live databases. Controlled identity/repository boundaries verify denied
  operations cannot write, stale identity/grants/roles after confirmation,
  anonymous ownership, local/imported origins, hidden ancestors, title boundaries,
  unhealthy stores, journal failure and commit-before-publication ordering.
- This target is part of existing `make check`, used by hosted Linux and native
  CI. Real storage/catalog/protocol behavior remains covered by
  `test_extended.py`, `test_content_audit.py`, `test_moderation.py`, and the
  existing signed multi-BBS/Web `test_shared_visibility.py` suite.
- Local Windows/UCRT GCC: Core unit test passed with `-Wall -Wextra -Werror`.
  The full runtime did not build because existing storage/OS code needs POSIX
  APIs such as `fcntl`; Windows runtime compatibility is not claimed. Do not
  change those unrelated modules or relax diagnostics for this refactor.
- Full runtime/OS results must be tied to the PR and resulting main SHA. Do not
  substitute the baseline's successful native runs for this extraction.

Reviewed documentation: `docs/manual/boards.md`, `board-admin.md`, `logs.md`,
`links.md`, and `help/{ja,en}/content.txt`. Command names, origin restrictions,
owner/SIGOP access, confirmation and audit behavior are unchanged; no public
manual/HELP wording needs to change for this internal step. Source-lock changes
cover the reviewed extraction and build integration, not a specification change.

The next read extraction is documented in [Structured article reads](ARTICLE_READ.md).
Structured list/search operations and Session UI adapters remain separate work. Auth/ACL must stay in the worker/Core rather
than being copied into MMM/Web/SSH. Neither UI selection nor future Door broker
identity is implemented by this PR.
