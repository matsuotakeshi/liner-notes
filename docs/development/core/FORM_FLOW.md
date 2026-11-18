# Application and profile form boundary

LN-23/24 grouped extraction based on main after PR #125,
`7ea34492427405968ee61cafb190dfedf14e697c`. This is a Core/UI refactor, not a new
registration policy or MMM implementation. No DB format or public command changes.

## Core responsibilities and ownership

`form_flow_core.c/.h` owns two independent event-loop values:

- `ApplicationForm`: the existing 14-field order, input/review step and staged
  8193-byte application buffer. Its API advances valid input, requests review,
  restarts input, hands off to password entry or cancels. It has no credentials.
- `ProfileForm`: the selected field and optional member target. Field selection is
  one-based at the API boundary, bounded to 13 editable fields / 14 search fields.
  GROUP is search-only in this form. A request is typed as self edit, member edit
  or search; search explicitly has no edit target even after editing another user.

Session embeds these values. Field/key accessors return borrowed immutable static
strings. `application_form_input` copies a nonoverlapping terminated value into its
own buffer; it retains no input pointer. A request's profile field pointer has static
lifetime. No handles, locks or allocations cross this boundary. Each connection
has its own form; the Session event loop remains the sole writer.

Application Core enforces the existing 320-byte/no-ASCII-control line bound, calls
no storage, and receives the field-specific validity result from the existing
Profile validator through Session. The complete appended row must fit, or no step
or body mutation occurs. Normal maximum fields fit far below 8193 bytes. Core tests
with supplied validity flags do not replace actual Unicode/date validation.

## Adapter and trust boundary

Session retains command spelling/case rules, Japanese/English prompts, legacy input
mode values, command normalization, connector status, numeric parsing and worker
submission. The profile adapter converts the typed intent to the existing named
PROFILESET / MEMBERPROFILE / PROFILEFIND command path. Core does not grant access:
Member/Core workers still enforce current actor/epoch, administrator status,
privacy, compatibility policy, search permissions, registration mode and storage.

An administrator's selected member is only intent. Reset still clears that target
on account transition/revocation. Search never inherits it. A selected field is not
an ACL. This does not add a new unauthenticated profile API or bypass normal commands.

Application review does not allocate an account or authenticate. Y only advances
to the existing hidden password prompt. The existing worker then creates/persists
or rejects the account/application, including approval-required registration.

## Preserved lifecycle

- Enter skips a field. Y/E before the review step remain field values, not actions.
- At review, Y/y confirms, E/e restarts, other input repeats the confirmation prompt.
  /CANCEL and /cancel retain the existing cancellation behavior.
- Restart resets the logical buffer and first field. Old logical values do not
  appear in the new review. No new reset-on-login or retry behavior is introduced.
- Cancel wipes all staged bytes. The password-processing path copies the application
  into its existing request then wipes the form, including on invalid password.
  The existing password-prompt cancellation path is not redesigned here.
- Secure clear uses volatile byte writes and needs no UI/OpenSSL dependency. Full
  Session disposal still uses the existing OPENSSL_cleanse. Application text is
  neither a password nor an authentication credential.
- Profile field 0 returns through the adapter; invalid/non-numeric/out-of-range
  choices redisplay the menu and do not alter the previously selected field.
- Existing packet markers, application field serialization, terminal output and
  WebUI behavior remain unchanged. HELP needs no wording change for this refactor.

## Tests and verification scope

Local Windows/GCC strict C11 build/execution passes for all application keys,
review/restart/confirm/cancel, unchanged state on invalid input, 320-byte boundary,
full-buffer refusal, bytewise erasure, all edit/search fields and target isolation.
Session and adapter tests pass strict syntax checking; Python UI test compiles.

The extended Japanese/English adapter test covers review, invalid field input,
restart without old logical data, password handoff/clearing, cancellation and target
revocation. Execution belongs to the existing Hosted CI. The existing real Telnet
stage69 UI suite now rejects GROUP as an editable field and exercises E/review/Y
registration through persistence, then reads back the final handle. Existing
normal/compatibility profile and registration tests remain in the regression suite.

`test-form-flow-core` joins `make check`. No additional full local regression or
native VM execution duplicates CI. Canonical manual generation, offline parity,
links and legacy mappings are verified locally. OS/package/runtime success must
be assessed for the PR SHA; no local-only result is presented as native validation.
