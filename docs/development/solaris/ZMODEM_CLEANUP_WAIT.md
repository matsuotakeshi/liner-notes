# ZMODEM interruption cleanup wait

Baseline: main `1d1b220b347996127f7d7d645ae32b0755e66b29`.
Solaris run [44045667771](https://github.com/matsuotakeshi/liner-notes/actions/runs/44045667771)
failed in test_zmodem_batch_resume.py at the two-file assertion following a fixed
1.5-second sleep. Artifact native-ci-solaris-results, ID 14313625060, retains
solaris.log and results.json. Remote work directory was
/export/home/ci-test/ci/native-solaris-XXYnnPSd. The log does not identify the actual
file count; delayed cleanup is a hypothesis, not an established root cause.

The test now waits up to 30 seconds on a monotonic clock, at 0.1-second intervals,
for the exact expected names in one pending data directory. It then obtains the
existing transfer lock nonblocking, without creating it, and rechecks the file
set while holding the lease. This prevents proceeding while the receiver still
owns the transfer. Native flock and the existing Solaris record-lock abstraction
are reused. The lease is released before the client reconnects.

Applied to both single-file and batch interruption checkpoints. File-count,
partial-size, final SHA256, publication and reduced resume wire-byte assertions
remain. The product's 8-second test-helper timeout, production transfer settings,
CI trust boundary and test selection are unchanged. No sleep is forced once the
required state is ready. Timeout failures report observed relative names/sizes
and last lock/file state; unexpected I/O errors fail immediately.

Local validation: six deterministic fake-clock/lease tests pass, covering immediate
readiness, delayed second file, delayed lock, missing/extra files, a permanently
busy lock and unexpected I/O errors. These do not prove live Solaris resumption.
The tests are included in make test-zmodem, before the existing real-wire suite.

Revalidation sequence: hosted Linux PR CI runs the real transfer suite; after
review/required checks and main integration, the existing Solaris Native CI runs
that exact main SHA. Do not run the PR on the home runner or rerun the old SHA as
proof of this change. Do not claim the original failure fixed until the modified
real-wire Solaris trial passes. One repair/retest cycle only; recurring failure
requires reviewing the new diagnostic output before further edits.

No production services, OS configuration, data, product implementation or release
were changed. Canonical manual content is unchanged; the Makefile source digest
was reviewed because only the existing test target gains a focused test command.
