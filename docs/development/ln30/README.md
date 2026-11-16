# LN-30: large-data failure investigation

## Diagnostic prerequisite (PR #14)

Baseline reviewed: main `1840d4458b39d7e9137dbec9a4aef8fb6c570490`.
The open PR's `Server` diagnostics were not present in this baseline. They remain
useful: the old startup path killed the child before recording its status and
reported an empty stdout line without distinguishing EOF from no line received.
The old shutdown path provided only `TimeoutExpired`, losing the bounded process
state snapshot. This is a defect in failure evidence, **not an established cause
of the article/mail memory failure**.

The updated helper records the status observed before cleanup, status after
cleanup, stderr, and (on shutdown timeout) `ps` state/RSS/elapsed time. A negative
pre-cleanup status denotes a signal on POSIX. If it is `None`, the process was
still running at that observation: the subsequent cleanup status must not be
misreported as the original crash signal. EOF alone does not prove a crash.

Run the small harness regression first:

```sh
python3 tests/test_server_diagnostics.py
# or
make test-server-diagnostics
```

It uses controlled child/process states to check nonzero exit, POSIX signal status,
clean exit without LISTEN, EOF while still running, no startup line, malformed
startup output, shutdown timeout, unavailable/slow ps, and unexpected stderr.
The existing `test-stage63` target includes it, so the established hosted/native
`make check` runs it without a new workflow or a duplicate VM invocation.

## Root-cause investigation remains open

`tests/test_stage63_memory.py` still generates 12,000 articles and 12,000 mails,
loads their bodies, searches, appends article/mail ID 12001 and marks SEEN 12000.
The RSS growth assertion remains strictly below 48 MiB. Neither the startup
deadline/environment override nor the five-second shutdown watchdog is increased.
Product code, data formats, authentication and ACLs are unchanged by this PR.

After reviewed code reaches main, use the existing required native CI. If the
failure recurs, retain only the failing stage63 assertion and its relevant nearby
output, exact source SHA, OS and run URL. Distinguish natural exit/signal from
cleanup kill and startup waiting from shutdown waiting before choosing a product
fix. Resource exhaustion, SQLite/index behavior and algorithmic complexity are
investigation candidates, not established explanations.

Do not label LN-30 fixed based on these diagnostics or on one passing run. Do not
raise limits, skip tests, repeatedly rerun full CI, or inspect production data.
The 2026-11-16 operator policy limits autonomous repair/retest to one cycle and
requires reporting/stopping when CI is pending for a long time, remains failing,
or needs infrastructure work. This prerequisite may therefore remain an open PR
until its required checks finish; later Core/UI/DB/auth/SSH work is not completed
by this change.
