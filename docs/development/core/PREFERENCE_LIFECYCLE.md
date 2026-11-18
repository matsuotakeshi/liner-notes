# Display preferences and exit lifecycle (LN-23/24, draft)

Baseline: main `12b04dca0656be4c36204e85fb0d9ff847dec4ac` (PR #121).
This draft extracts display preference snapshots, legacy auxiliary encoding,
dirty comparison and save-before-exit decisions. No storage schema, preference
limit, authentication rule, command or UI selection policy changes.

## Boundaries

`preference_core.c/.h` owns value/record conversion and `PreferenceLifecycle`.
The state is private to a Session event loop; functions perform no allocation,
IO, authorization or persistence. Values are borrowed for each call and returned
records own their scalar fields. The legacy signature is a comparison value,
not a security credential. `preference_unpack` keeps both current dimensions
when either stored dimension is zero. Session applies the existing terminal
bounds when both dimensions were supplied.

Session builds explicit PREFS and disconnect payloads from the same snapshot.
`session_disconnect_preferences(session, NULL)` tests whether a request is
needed. With a separate caller-owned BoardRequest it zeroes that request and
fills a save operation only when eligible, retaining user ID and credential
epoch. Runtime owns heap allocation, queue submission, logging and teardown;
it no longer reads the preference lifecycle fields or duplicates serialization.
The worker and Member Core still revalidate the member and epoch, validate
values and perform the actual storage operation. A snapshot is not an ACL grant.

## Preserved behavior

- Language, pager, editor, dimensions, line numbers and transfer protocol retain
  their old bit encoding. Menu mode is still saved by its separate command.
- The comparison baseline is observed at login/load, as before. Explicit save
  and save-before-exit do not introduce a new baseline update or deduplication.
- Guests do not save on disconnect. A successful exit-save uses the existing
  recursion guard, then LOGOUT or QUIT clears it in the existing order.
- Failed queue submission clears the requested exit. Failed save replies keep
  the member connected. Success routes back to the requested exit operation.
- Completion identity/slot validation precedes lifecycle handling. Disconnect
  snapshots retain credential epochs; invalidated members cannot bypass the
  existing worker authorization by disconnecting.
- No attempt is made to change old retry, teardown or duplicate-save semantics.

## Verification and resumption

Local Windows: 1,008 combinations of encoding/decoding, zero dimensions,
per-field dirty comparisons, guest exclusion, queue/save failure, recursion
guard and session isolation passed in `test-preference-core.exe`. Strict C11
Session/adapter syntax and Japanese/English catalog checks passed. Adapter
regression additions cover disconnect payload/epoch, restore/clamping, stale
completion, save failure, logout and quit, but runtime execution awaits CI.
`make check` includes the new direct test; existing preference, reading settings,
disconnect and full Session tests remain enabled.

Runtime syntax on MinGW fails on pre-existing `struct flock` declaration and
Windows TZ indentation warnings. The same command on the PR #121 baseline
produces the same two diagnostics. These were not suppressed or changed; local
syntax is not Linux/FreeBSD runtime verification.

Work stopped after main's Rocky package run failed on an external EPEL metadata
HTTP 404. Release Set failed because that exact-SHA RPM run failed:

- Rocky: https://github.com/matsuotakeshi/liner-notes/actions/runs/44349094901
- Release Set: https://github.com/matsuotakeshi/liner-notes/actions/runs/44349487429
- Native CI was still running at the one-time status check:
  https://github.com/matsuotakeshi/liner-notes/actions/runs/44349094914

No automatic retry, mirror change or native PR testing was performed. Resume by
checking the exact main SHA's outstanding runs and, when authorized, retry the
failed Rocky job after the mirror recovers, then reverify Release Set. Review
this draft and its hosted PR CI separately; successful main checks are not
validation of this draft's commit. No release or production operation occurred.
