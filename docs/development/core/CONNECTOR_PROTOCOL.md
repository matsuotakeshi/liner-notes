# Connector protocol boundary (LN-23/24)

This unreleased refactor builds on connector buffer/binary Core and extracts the
remaining negotiated-line adapter from Session. It does not implement SSH, MMM,
a structured operation API for WebUI, or a new link protocol.

## Responsibilities and lifetime

- `connector_protocol.h/.c` recognizes OPEN/SYNC/LITERAL/BLOCK/LINKBLOCK/DATA/TRANSFER
  envelopes, decodes LINK requests into `LinkWireRequest`, and serializes a
  `ConnectorState` snapshot. It depends on C/OpenSSL and existing ID limits, not
  Session, UI, workers, storage or authentication.
- `session.c` owns the marker, selects the current state, checks negotiation and
  authentication/editor/password/chat/gather restrictions and link membership,
  and maps a parsed request into BoardRequest. Board/Link Core continues to check
  authorization, ACL, origin and event semantics. Parsing grants no authority.
- Envelopes borrow bytes from the input until modified. The marker is absent or
  exactly 64 bytes plus NUL. Envelope identification uses the explicit length;
  payload conversion to numbers/commands is performed only for terminated input.
- LINK parsing mutates its terminated input, copies metadata into a small result,
  and decodes directly into the caller's body buffer. It avoids a second 256KiB
  stack copy. Failure zeroes the result and empties the output. Decoding scratch
  is freed before returning. Buffer capacities are checked and Session asserts
  metadata field compatibility at compile time.
- Snapshot formatting borrows a validated login name and nonce only for the call.
  It returns no partial successful response. Session aborts on output overflow.
  It includes no password, input body, or negotiated marker.
- These functions have no shared mutable state, worker access or asynchronous
  ownership. The runtime scheduler still applies the previous worker result
  before dispatching the next request. Block/binary lifetimes remain in
  Connector Core and Session disposal.

## Compatibility and trust boundaries

Command order, case, prefix lengths, lower-case marker/nonce validation, accepted
Base64 forms, lookup token handling, numeric upper bounds and protocol 1 JSON
field order remain unchanged. LOOKUP still leaves identifier semantics to Core.
The link event signature/synchronization protocol 2 is unchanged. Invalid LINK
requests use the existing syntax error; denial precedes parsing. Local HIDE and
origin withdrawal visibility remain in existing Link/Article Core.

The fallback for a pending LITERAL remains after envelope dispatch: SYNC can
inspect secret/editor state without consuming user content; the next literal
line rechecks editor eligibility. A wrong marker does not authorize a connector
request. Recognizing a marker alone is not authentication.

## Validation

`make test-connector-protocol` checks every envelope, missing/wrong markers,
truncation, nonce/case rejection, every LINK action, ID/time bounds, malformed
Base64, forbidden controls/NULs, Japanese metadata, exact 256KiB body capacity,
failed-result clearing and byte-exact state JSON including output size bounds.
`make test-adapters` adds Session-level unauthenticated LINK/BLOCK rejection,
SYNC during secret input and LITERAL state revalidation.

The new direct target is part of existing `make check`. Existing WebUI connector,
block transfer, binary transfer and Link integration tests remain enabled. Local
Windows validation covers the pure C test, strict syntax checks of Session and
adapters, i18n catalog checks and canonical manual. Full adapter/runtime and OS
integration are left to existing hosted PR CI and trusted-main native CI; local
syntax success is not an execution result for those tests.
