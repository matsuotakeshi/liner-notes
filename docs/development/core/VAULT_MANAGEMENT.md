# Vault management Core boundary

Base: `2766d8ef23076d4e18786862c163e1361e2d35a6` (PR #105 merge).
Grouped LN-23/24 increment: file mutations/copy, board policy/metadata,
read-versus-edit admission, passwords/unlock and download retention coordination.
This is not completion of all Core separation or a release.

## Responsibilities and lifetime

- `vault_bridge.c`: current command-to-operation mapping, delimiter/ON/OFF
  parsing, root `-` conversion and unchanged response/error formatting.
- `vault_management_core.c/.h`: typed operation dispatch, editor admission,
  bounded owned copy/board/grant results, metadata-then-retention coordination
  and distinct retention-failure outcome. No Session, UI text or socket API.
- `vault.c`: authoritative operation-specific ACLs, inherited SIGOP scope,
  live guards, password hashing, policy grammar, journal and payload operations.
  Existing storage logic is reused; this PR does not duplicate or relocate its
  private file/board structures. Metadata values retain the repository grammar,
  including options and ACL expressions; this is not a new terminal parser.
- Download repository: existing receipt pruning and cumulative totals remain
  unchanged. The Core invokes pruning only after an OPTIONS mutation succeeds.

The existing vault worker serializes calls on its one store. IdentityProvider
must resolve a trusted current Principal for every request; callers may not
construct capabilities or reuse an old session grant. The board worker still
resolves target member numbers to subjects before owner/SIGOP changes. The vault
worker keeps its epoch/session guards and wipes password request bodies after
processing. Core borrows input strings only for the call and does not retain,
log or return passwords. Startup/shutdown must remain quiescent.

Result snapshots are owned by the caller and contain no borrowed pointers.
Inputs must not alias outputs. Failed copy, inspection and unlock clear all
output fields; failed metadata updates report an explicit outcome, not a fake
successful grant. A successful inspect does not confer authority for a later
save: every save runs repository checks again. Unlock returns board/revision
only after verification; existing session code owns its connection-local grant.

## Permissions and compatibility

No blanket administrator requirement was added. Owners may perform the existing
allowed description/attribute/delete operations; SIGOPs act only within their
current inherited scope; global administrators retain their existing authority.
The repository checks source/destination management for copy/category changes.
Names/subjects are vault namespace values, not UI login names or member numbers.

File DESCRIPTION/RENAME/OWNER/ASCII/CATEGORY/HIDE/RESTORE/DELETE retain their
individual repository validation and error ordering. Invalid ASCII values still
reach vault_ascii rather than being replaced with a different admission result.
Copy still checks its original payload, retains metadata, allocates a new ID and
records COPY/ASCII properties. Failed copy is not automatically retried.

Board NAME/GUIDE/SIGOP/OPTIONS map to the same internal metadata kinds. Existing
scope delegation, limits and MENU-with-files rejection remain in vault.c.
Inspection for display returns the current readable configuration; inspection
for editing additionally checks can_manage and clears rejected guide content.
Empty password still removes the password via the existing operation.

Every existing wire response is retained, including FILEBOARDSETTING's compact
versus detailed forms, FILEBOARD EDIT, FILEBOARD UNLOCKED, FILE COPIED and all
mutation markers. RAW/Telnet/Web callers keep their existing adapters. No schema,
IDs, saved permissions, encryption scheme, worker concurrency or UI changes.
ASCII preview and transfer dispatch remain in the bridge for a later increment.

## Existing partial-failure boundaries

OPTIONS metadata is committed before downloads_prune. If pruning fails, Core
returns `VAULT_STORAGE` with `retention_failed=1`; the adapter retains
`ERR vault log storage`. No rollback, retry or further mutation is performed.
Other metadata failures do not prune. This is distinct from a generic storage
error and explicitly covered by a failure-injection test.

Cleared output is not a storage transaction guarantee: existing copy appends its
file record before later protocol/attribute events; physical delete records its
tombstone before removing the payload. A later failure can leave committed
state. This refactor preserves those boundaries, and does not claim fault-atomic
copy/delete or repair failed payloads. The previous download receipt/prune retry
issue in DOWNLOAD_SERVICES.md remains a separate follow-up.

## Verification

- `make test-vault-management-core`: six direct-API groups with repository stubs;
  operation/error forwarding without a global-admin gate, copy snapshot cleanup,
  policy/metadata routing, metadata-before-prune ordering and injected failures,
  inspect/edit denial, password removal/unlock cleanup, invalid context admission.
  Stubs test orchestration, not the real repository's permission implementation.
- `tests/test_vault_management_core.py ./runtime ./vaultctl` joins `test-stage65`:
  real RAW worker/identity/storage tests for journals and authoritative SQLite;
  owner versus reader/inherited SIGOP, cross-scope copy denial, payload/metadata
  preservation, hide/restore, physical reclaim with permanently unused IDs,
  multi-line guide edit, successful receipt retention without lost totals,
  parent password rotation/removal, live delegation/revocation, audit scope and
  restart persistence. Existing Telnet/Web/vault tests remain enabled.
- Local Windows GCC: six direct-API groups passed; bridge/Core syntax passed
  using declaration-only POSIX shims (not committed). Python fixture compilation
  and diff checks passed. Full POSIX runtime/socket tests are pending standard
  hosted CI; this is not a Windows runtime validation claim.

Only trusted main runs native VM CI. No CI settings, production services,
deployment, package versions or release publication are changed.

Canonical manual validation passed: 42 pages / seven chapters, 2,812 internal
links, 172 legacy URLs, 622 legacy anchors and Markdown/HTML/offline parity.
