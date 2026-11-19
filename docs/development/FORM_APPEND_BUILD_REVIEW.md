# Application form append review (2026-11-19)

The optimized Ubuntu build rejected `application_form_input` because GCC's
format-truncation analysis could not prove the remaining snprintf capacity.
The existing code checks the complete record length before appending; no runtime
overflow or game authentication failure was demonstrated by this build error.

The fix copies exactly the validated key/value lengths and writes the same pipe,
newline and NUL. The 320-byte input limit, control-character rejection, capacity
check, field progression, review/restart/cancel and profile APIs are unchanged.
Tests additionally cover a record that exactly fills the buffer including NUL
and a one-byte shortage that must preserve the entire form state.

Reviewed `docs/manual/accounts.md`, `docs/manual/personal-settings.md`,
`src/form_flow_core.h` and the existing form tests against this diff. The public
registration and profile instructions remain accurate; no public behavior,
configuration, HELP or storage-format change is needed. Refresh only the two
changed source/test fingerprints in the canonical manual source lock.

Local optimized GCC form tests passed with warnings as errors. Ubuntu and other
Hosted checks must validate the updated commit; earlier CI success is not reused
as evidence for it. This change does not authorize main merge or production use.
