# Vault query services Core boundary

Base: `9e8ab08bfaee9db4e86ea579e336c957806207f7` (merge of PR #104).
This grouped LN-23/24 increment covers file listing/search/new-file queries,
batch selection, hierarchy, detail, category/board enumeration and audit pages.
It does not claim complete Core separation or a release.

## Responsibilities and trust boundary

- `vault_bridge.c`: resolves identity for every request, parses legacy category
  delimiters and dates, and formats existing markers, timestamps and metadata.
  It still handles mutations, ASCII text preview and transfer dispatch.
- `vault_query_core.c/.h`: synchronous typed query services, current scope
  admission, descendant versus exact-board selection, date-window filtering,
  bounded aggregate results and limits. No terminal text, command parser,
  Session, socket, Base64 or storage layout dependencies.
- `vault.c`: existing namespaced principals, current capabilities, inherited
  board policy/password grants, hidden-file filtering, locking and replay.
  This remains the authoritative ACL/storage layer; it is not duplicated.
- `download_core`/repository: transfer tickets and durable receipt counts remain
  unchanged. The legacy file list still obtains its download counts there.

The single vault worker owns the store and serializes these operations. Open and
close only at quiescent startup/shutdown. Inputs and their strings are borrowed
for the call; output snapshots are caller-owned, bounded and contain no pointers
into mutable storage. Inputs must not alias outputs. All errors clear the entire
result, including partial rows and continuation/aggregate values.

The API requires a Principal freshly resolved by the trusted IdentityProvider;
UI/transport input must never manufacture subject, capability or password grants.
The bridge retains disabled-store then login/epoch admission before parsing.
Each repository operation rechecks its own ACL; a successful scope check is not
a reusable access grant. Audit retains CAP_VAULT_MANAGE admission, distinct from
ordinary CAP_VAULT_READ. Neither board SIGOP nor list visibility grants global
audit access. Core null-context/argument checks do not replace authentication.

Queries spanning repository calls retain existing consistency: metadata and
protocol are checked separately, as are scope and subsequent pages. This PR
does not introduce a cross-page database snapshot. A concurrent external change
can affect a later call/page; no claim of transaction-wide snapshot isolation.

## Compatibility details

- FILES continues to use an empty-pattern vault_search, not vault_list.
- NULL scope means all readable categories; empty scope is an explicit root.
  Scoped SEARCH/NEWS admit the selected board first. Missing/denied/storage
  errors from that admission remain DENIED, ahead of invalid-date errors.
  FILESIN keeps its existing repository scope validation and error ordering.
- Date parsing remains in the adapter. Core accepts a validity flag and numeric
  interval, so scope admission still precedes a malformed date result. Upper
  bounds are inclusive; filtering does not recompute the scan continuation.
  An empty window page can have a nonzero next cursor. Unused result rows are
  cleared so excluded entries cannot leak through a future structured adapter.
- Batch wildcard matching reuses vault_search (including existing DOS masks),
  then filters exact category when scoped. Descendants are not batch-selected.
  No new selected-board precheck is inserted into the legacy MATCH flow.
  The existing effective BATCH_FILES/FILE/BATCH_BYTES limits apply; crossing any
  limit fails the whole selection. Sixteen 1GiB files fit the 16GiB ceiling.
- CHILDREN preserves whole backend page consumption: with seven children
  already collected, the final eight-row page can yield fifteen children.
  The owned output therefore has fifteen slots, not a truncating eight-slot
  limit. The same cursor, direct-child filtering, ordering and display-name
  fallback are preserved. Non-root admission failure remains DENIED.
- Detail checks file metadata then registration protocol; any protocol lookup
  failure after successful metadata still maps to STORAGE. The failed result
  contains neither metadata nor protocol. Hidden/inaccessible files still use
  the repository's NOT_FOUND result; no existence disclosure is added.
- Boards/categories keep eight-row pages; audit keeps four-row pages. Legacy
  event E before/after suppression and local-time formatting remain in the
  adapter. Audit cursors and all command strings are unchanged.

No schema, IDs, journal format, file payload, public limits, transfer algorithm,
worker concurrency or release version changes. The manual/JA/EN help clarify
existing query scopes and global audit access, not a new public feature.

## Verification

- `make test-vault-query-core`: six isolated direct-API groups with controlled
  repository stubs. Scope distinction/admission/error ordering, date filtering
  and empty-page continuation, exact batch matching, all three configured
  limits and 16GiB arithmetic, partial-backend failure cleanup, fifteen-child
  pagination, detail protocol failure, read versus manage-only audit admission.
- `tests/test_vault_query_core.py ./runtime ./vaultctl` is in `test-stage65`:
  real RAW sockets and IdentityProvider, guest/member/admin, both journals and
  authoritative SQLite, recursive query versus root batch selection, hidden and
  inherited private board denial, list/detail/audit privacy, pagination and
  persisted changes after restart. Assertions reject unexpected ERR responses
  immediately instead of waiting indefinitely for an end marker.
- Existing vault catalog/board/password/transfer, Telnet, WebUI and package
  checks remain enabled in standard CI. No supplemental workflow or CI trigger
  changes are introduced; no home VM receives unreviewed code.

Local Windows GCC direct-API tests passed all six groups. C syntax checking of
Core and bridge passed with declaration-only Windows POSIX shims; the shims are
not source/package changes. Python fixture compilation passed. The full POSIX
runtime/socket test is pending hosted CI, not claimed tested on Windows.
Native results on the earlier main commit do not validate this branch.
No production access, deployment or release was performed.

Canonical manual generation/check passed: 42 pages, seven chapters, 2,812 local
links, 172 legacy URLs, 622 legacy anchors and Markdown/HTML/offline parity.
