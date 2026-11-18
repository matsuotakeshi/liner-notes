# Classic terminal presentation boundary (LN-23/24)

## Responsibility

This change separates presentation policy from Session, not business authorization.
`classic_input.c/.h` implements existing RTBBS-style echo case policy and submitted
command normalization. `terminal_pager.c/.h` implements dimension normalization,
UTF-8 terminal-cell wrapping and response selection for paged display. Neither
module depends on Session, Ui, BoardRequest, storage, sockets, authentication or
OpenSSL. They cannot invoke BBS operations or obtain protected article content.
This is not MMM or a new UI-selection feature and does not define a shared command
syntax that future independent UI profiles must use.

Session retains trusted mode snapshots, dispatch, editing, secret handling,
identity/ACL context, actual I/O, translated prompts, pager enable/auto-read rules,
logical reading/search cursors, retained response allocation and secret cleansing.
Only complete responses already authorized by business Core enter the pager.
Response-prefix classification is a presentation choice, never an access check.
Transport still calls session_uppercase before echo; session_line still validates
line limits/NUL and processes the connector path before classic normalization.

## Input contract

ClassicInput is a synchronous borrowed value snapshot. Selection dialogs that
uppercase all input are mapped explicitly; their internal numeric enum values do
not enter the adapter. Prefix inspection accepts exactly size bytes and does not
require a terminator. Submitted lines must have a terminator at size and belong
to the caller. The module owns no buffer and keeps no global mutable state.

Existing precedence remains: exact /CANCEL folds even in secret or memo input;
otherwise memo/detail forms/secret input retain case. File action/batch selection
keeps its existing uppercase behavior. Editing/chat/talk/Gather distinguish slash
commands from ordinary data; double slash bypasses normalization. Named command
arguments, ordering/scope keywords and supported confirmation tokens keep the
existing contextual rules. No new permission, password interpretation or parser
is introduced. Future UI adapters need not inherit these historical rules.

## Paging contract

Dimension ranges stay 20..240 columns and 5..100 rows, with independent fallback
to 80/24. Display reserves one column and three rows exactly as before. UTF-8 cell
counting uses the existing locale-independent approximation for wide characters,
combining marks and joiners; it is not a Unicode grapheme/ANSI terminal emulator.
Malformed/incomplete UTF-8 retains the existing one-byte fallback behavior.

Caller owns a NUL-terminated authorized source and an offset inside it; output
must not overlap source. Rendering returns complete/more and advances only the
source offset. CR, LF and CRLF display as CRLF. UTF-8 sequences are not split to
fit the output buffer. A capacity below five bytes fails without consuming data;
the actual Session buffer remains 8192 bytes. Wrapping state resets per screen
fragment as before. Completion/abort still clears retained text in Session and
cannot change the logical article cursor or grant access.

## Verification and remaining boundary

`make test-terminal-policy` is included in make check. Explicit contract cases
cover menu syntax versus data, password and memo/form isolation, editor/chat
commands and slash escapes, ordering/confirmation arguments, bounded prefixes,
dimension boundaries, CR/LF, ASCII/Japanese/combining/emoji width, malformed UTF-8,
small output buffers, independent cursors and paged/non-paged response types.
Session adapter tests additionally check actual mode mapping, dimensions and
existing retained-page clearing on revocation. Existing socket tests test_pager,
test_list_pager, test_stage59_ui and editor/reading tests remain in normal CI.

Local direct tests, strict syntax checks, catalog and canonical-manual validation
are separate from full socket/native validation, which runs in existing CI.
No production or unreviewed native-VM execution is used. Command dispatch and
more complex editor/menu interaction still reside in Session; this change does
not claim that the full Core/UI separation is complete.
