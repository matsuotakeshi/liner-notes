# Editor stack exhaustion in main native CI

## Exact failed revision and evidence

- Source: `38dc1c15d27687e568240de54c1bae2dbce2d1df` (merge of #118).
- Run: https://github.com/matsuotakeshi/liner-notes/actions/runs/44323092888
- Artifact: `native-ci-results`, ID `14435993092`.
- ZIP SHA-256: `7a7aa927fc27f73ae3e59dd55f38d3fba4b6c4789807565f66c7776e349f4669`.
- Rocky and Ubuntu compiled successfully, then test-adapters terminated with
  SIGSEGV. FreeBSD native regression passed; hosted Linux and package checks
  passed on the same SHA. Those successes do not validate the failed native runs.

The existing trusted-main binary on Rocky was inspected with the CI account,
verified `.ci-source-sha`, shared native host lock and bounded GDB execution.
No unreviewed code was copied to a home VM. The trace ends in `emit`, called from
`edit_draft` for `/REPLACE 1 Mixed Case`, then command_core, command, session_line,
and test_adapters main at line 203. Both Linux hosts report an 8192KiB stack.

Observed frame reservations in the Rocky binary:

| Function | Stack subtraction |
| --- | --- |
| test main | 0x3a29f8 |
| command_core | 0x44608 |
| edit_draft | 0x202048 |
| emit | 0x202018 |

Together with intermediate frames these exhaust the native Linux stack. The
large automatic listing array is reserved at edit_draft entry even for REPLACE.
This is not a compilation timeout, disk-space failure or an out-of-memory claim.
A container stack limit was not captured for the successful hosted run; an
inherited larger Docker limit is a possible explanation, not verified evidence.

## Change and verification

Allocate the CONTENT_DISPLAY scratch buffers only in the draft/line listing
branches and free after synchronous output. Non-list editing commands no longer
carry those frames. Allocation failure uses the existing board-busy message;
invalid view results and output failure still release memory. Session document,
Undo, publication behavior and buffer capacity are unchanged.

Set the existing hosted Linux regression shell's stack limit to 8192KiB before
make check. This matches the native limit and is stricter than an unlimited
container default; no test is skipped, timeout increased or stack limit raised.
Add /LINES execution to the existing mixed-case editing/Undo/list/search adapter
sequence. Direct editor interface tests, Session/adapter syntax and i18n checks
pass locally. Hosted 8MiB and native runtime success remain pending CI; do not
claim that local Windows syntax tests prove the Linux crash resolved.

## Resume and diagnostic commands

Existing failed copies (preserve for evidence):

- Rocky: `/home/ci-test/ci/native-rocky-KFExoWkw/source`
- Ubuntu: `/home/ci-test/ci/native-ubuntu-srEs0ghO/source`

Read `.ci-source-sha` and verify the above SHA before diagnosing a copy. The
bounded reproduction used on Rocky, through the existing CI SSH account, is:

```sh
cd /home/ci-test/ci/native-rocky-KFExoWkw/source
ulimit -c 0
LC_ALL=C flock -n /home/ci-test/ci/.liner-notes-native.lock timeout 25s \
  gdb -nx -batch -ex 'set pagination off' -ex run -ex 'bt 8' --args ./test-adapters
```

Run the fix through hosted PR CI first. Only after successful review/merge run
trusted-main native CI for its exact SHA. Do not rerun the unchanged failed main
merely to seek a green result. Subsequent refactoring PR #119 should wait for this
fix and update its base through the usual PR/CI process.
