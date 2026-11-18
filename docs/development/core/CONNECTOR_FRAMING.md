# Connector framing and assembly Core (LN-23/24)

## Responsibility and trust

`connector_core.h/.c` owns bounded large-command assembly and binary-v1 framing.
It has no Session, BoardRequest, UI, socket, OpenSSL, database or account dependency.
It allocates only a declared command block, never a complete uploaded file.
Session maps decoded events to existing BoardRequest operations and retains:

- CONNECTOR OPEN/SYNC/LITERAL negotiation and exact token matching;
- login, editor/password/chat restrictions and board-link enablement checks;
- the allowed FILEPUTBLOCK/MFILEPUTBLOCK/LINKAPPLY command routing;
- the L-frame transfer command allowlist;
- authenticated request identity/epoch, worker dispatch and result handling;
- actual abort/dispose, terminal negotiation, output and menu restoration.

Parsing success is not permission to read/write a store. Existing workers/Core
revalidate actor, ACL, ownership and link authority at operation time. SYNC tokens
are response delimiters, not credentials. No signature, protocol 2, persistence,
public command or configured size limit changes are introduced. This extraction
does not add SSH, MMM or a network listener.

## Ownership and events

Each event-loop Session owns one zero-initialized ConnectorBlock and ConnectorReader.
No worker shares them. Block begin replaces and frees an existing allocation,
including when the new length is invalid. Allowed declared lengths are 1..90000
for transfer commands and 1..360000 for link commands. Append checks remaining
capacity before copying; overflow clears the block without publishing a prefix.
Take succeeds only at exact completion, appends NUL and transfers allocation
ownership to the adapter, which executes the existing command route and frees it.
Incomplete take leaves ownership unchanged. Dispose/expiry call clear exactly
as before. The 60-second block deadline is absolute from begin, not refreshed on
DATA receipt. A transferred/cleared block has no deadline. Heap allocation failure
still aborts the session; allocation-failure injection is not claimed tested.

Binary reader accepts one byte at a time, validates '~' and big-endian length
1..1024, then emits Q, L, R or W. L forbids NUL/CR/LF; Session checks its command
name. R requires 8 ID/offset bytes; W also requires 1..768 data bytes. ID/offset
range/ownership checks remain downstream. A decoded payload borrows reader storage
until the next feed/enter. It must be copied into BoardRequest synchronously.
Malformed/incomplete events clear the output descriptor; malformed input requires
the adapter to abort, not attempt resynchronization. Independent readers cannot
share partial input. Enter sets idle expiry to now+60s; first '~' sets partial
expiry to now+15s; a complete frame or worker response renews idle expiry.

Output encoding uses the existing optional kind byte, 4-byte big-endian length
and caller-supplied BOARD_REPLY bound. Overflow/insufficient space is rejected
before writing with written=0; payload must not alias output. Non-null object
pointers, initialized state, valid payload ranges and monotonic milliseconds are
caller obligations. Full Session disposal retains its existing secret cleansing.

## Tests and execution boundaries

`make test-connector-core`, also in `make check`, covers exact/max/oversize blocks,
replacement, ownership transfer, independent instances, absolute deadlines,
all-byte write payloads, incremental frame reception, mode-specific lengths,
malformed headers/commands, saturation-size inputs and output buffer guards.
`test_adapters.c` checks Session expiry and disposal plus existing binary deadlines.
`test_web_blocks.py` adds independent partial blocks, replacement and overflow
cleanup without permitting normal BBS commands. `test_binary_transfer.py` extends
malformed-frame refusal while retaining RAW/Telnet byte roundtrips, interrupted
upload resume and menu restoration. Existing link and Web connector tests retain
permission, framing synchronization, literal content and shared-board coverage.

Local direct C tests and strict syntax checks pass. Real Session linking, sockets,
Web dependencies and full regression are delegated to existing hosted PR CI.
Native CI remains trusted-main-only; no production or home VM runs PR code.
