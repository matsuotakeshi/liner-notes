# Linux Door Broker service candidate

This is a review/CI runbook for the unreleased candidate, not approval to enable
production. Existing legacy Doors remain the default. Selecting the isolation
socket routes **all** Doors through separate slot UIDs, including games without
Broker authentication. MMM and WebUI are outside this change.

## Installation boundary

RPM/DEB/source installation supplies root-owned, executable entrypoints:

- `PREFIX/libexec/liner-notes/door-broker`
- `PREFIX/libexec/liner-notes/door-isolation`
- unit **samples** in `PREFIX/share/liner-notes/door-services/`

Here PREFIX is `/usr` for distribution packages, `/usr/local` for the default
source installer. The wrappers use the package's fixed Python and scripts with
`-E -s -B`. No setuid executable, account creation, automatic enable/start, or
reset of sockets/reservations is added. RPM manifest generation includes the
entrypoints. An upgrade must retain locally reviewed unit files and policy JSON.

Provision separate non-login identities for BBS, Broker, consumer game server,
and **each concurrent slot**. Slot GID must equal its dedicated UID. Never share
these identities with another BBS, game service or scheduled task. Broker belongs
to the consumer socket group; slots have no supplementary groups. Check actual
numeric IDs, not the example numbers in `DOOR_BROKER.md`.

| Path | Owner / mode | Purpose |
| --- | --- | --- |
| `/etc/liner-notes/door-broker.json` | root / 0644 | UID and Door allowlist, no secrets |
| `/etc/liner-notes/door-isolation.json` | root / 0600 | fixed executable/slot policy |
| `/run/liner-notes-door-broker` | Broker / 0755 | systemd runtime directory |
| `/run/liner-notes-door-isolation` | root / 0755 | supervisor runtime directory |
| `/var/lib/liner-notes-door-isolation` | root / 0711 | persistent reservation and private drop directories |
| `/var/lib/liner-notes-door-slots/SLOT` | slot UID:GID / 0700 | game working directory |
| game wrapper and its ancestors | root, not group/other writable | fixed executable |

The configuration admission now checks working-directory ownership and every
ancestor too. A symlink, foreign owner or group/other writable ancestor is an
error. Do not solve it with recursive chown or mode 777. Preserve game data and
grant only the intended slot access. The wrapper and interpreter must not live
inside writable game data.

## SELinux boundary

Optional policy version 0.2.0 adds `liner_notes_broker_t`,
`liner_notes_isolation_t`, and `liner_notes_consumer_t`. The three new domains do not join the generic `daemon` attribute, so the
OS-wide cluster-mode daemon socket permission is not inherited. Explicit
systemd and NoNewPrivileges transitions remain required. Existing BBS and Door
domains remain `liner_notes_t` / `liner_notes_door_t`.

| Source | Destination | Permitted action |
| --- | --- | --- |
| BBS | isolation socket | authenticated launch request and PTY descriptor |
| isolation supervisor | Broker socket | issue and hold authorization lifetime |
| consumer game server | Broker socket | consume/check, constrained by kernel UID and Door allowlist |
| Door | consumer socket | game-specific authenticated protocol |
| Door/BBS | Broker socket | denied |
| Door/consumer | isolation socket | denied |
| Broker/isolation/consumer/Door | BBS data | denied |
| Door | persistent reservation files | denied |

Only the supervisor receives bounded setuid/setgid/kill/chown/DAC-override/fowner
capabilities. There is no SYS_ADMIN/SYS_PTRACE, unconfined attribute or permissive
domain. Broker and consumer have no capabilities or general TCP rule. The new services
use interpreter execution rules without `sys_resource_type` membership; the
root supervisor also receives no `sys_resource` permission. A remote
consumer is unsupported. Game server network/extra dependencies need a reviewed
local extension; the fixture consumer does not prove arbitrary games run under
this policy. The existing Door network boolean remains explicit and off by
default. Kernel UID checks still apply even when a socket label permits access.

Distribution prefix `/usr` has vendor file contexts. For a custom prefix use
explicit administrator-local `semanage fcontext` mappings after reviewing each
entrypoint. Do not relabel the entire libexec tree as a game or Broker domain.
Consumer entrypoint, socket directory and game data require explicit local
mappings to `liner_notes_consumer_exec_t`, `liner_notes_consumer_run_t` and
`liner_notes_door_data_t`; the package cannot infer an external game's layout.

`liner-notes-selinux --relabel` is explicit, honors local mappings and does not
use `restorecon -F`. Package updates label the fixed executable entrypoints only;
they do not recursively relabel existing data. Policy removal refuses while any
old or new confined domain is alive. Keep local policy overrides and labels.

## Controlled startup and shutdown

After exact-SHA native acceptance and a separate activation decision, review
and copy the samples to `/etc/systemd/system/`; do not overwrite an existing
local unit. Complete the account, policy, directory and SELinux labels first.
The unit preflight uses `--check-config`. It validates policy without opening a
listener or clearing stale state; both services remain `Restart=no`.

1. Inspect the package versions, wrapper paths, UID separation and configuration.
2. Validate unit syntax with `systemd-analyze verify` and reload definitions.
3. Start Broker, then isolation. Check their service state, process labels and
   `--check-ready` using the appropriate service identity. A pathname alone is
   not readiness; the probe verifies a live peer UID without issuing a ticket.
4. Start the registered game server and verify its consumer identity/label.
5. Enable the reviewed BBS isolation socket setting, then admit test sessions.
   Verify two different users, rejection of another member's mutation, and
   return to BBS. Do not fall back to a legacy same-UID Door if this fails.
6. For shutdown, stop admitting Door sessions, end BBS Door connections, stop
   isolation and wait for worker cleanup, then stop the consumer and Broker.

Do not replace `KillMode=process`, `SendSIGKILL=no`, or the supervisor's unlimited
stop wait with an automatic forced group kill. A stuck cleanup is a fault needing
inspection, not permission to clear reservations. Stop timeout monitoring must
alert without reusing slots. The samples do not enable on boot automatically.

## Crash recovery and update

Broker restart invalidates all tickets/leases; existing consumers must recheck
before every authoritative mutation. Supervisor death closes its watch pipe;
surviving workers revoke and reclaim their own game trees. Worker death can leave
a detached game and a durable `BUSY` marker. The next start must refuse that slot.

Before manually recovering **one** slot, stop admission and preserve logs, policy,
UID/slot mapping and marker contents. Prove the owning supervisor and worker are
gone, no task with the slot real/effective/saved UID remains, and an exclusive
nonblocking lock is obtainable on the original regular, root-only, single-link
marker. Investigate the cause and stop any confirmed owned game process first.
Only a separate administrator decision may clear the proven-unused marker.
Never delete all markers, reuse UIDs, or infer cleanup from an empty process
group. Socket recovery likewise requires proving the original service has exited
and checking that the exact orphan socket still has the expected owner/inode.
There is intentionally no automatic reset command.

Before updating: stop admission and services in the order above, save the exact
packages, policies, local units, game data and reservation evidence. Keep old and
new wrapper/scripts/policy versions paired. Do not hot-replace Python modules
while services run. After update repeat preflight, label and denial checks;
on failure keep admission closed and restore the complete known-good set. Do
not reset games or the BBS database. A policy downgrade is not allowed while a
new confined process is alive.

## CI and evidence

- Portable tests: 128 active members across eight generations, 128 concurrent
  issuer handlers, replay/scope/revocation, malformed input, resource fault
  injection and service generation/stop handling. These are bounded functional
  load tests, not a maximum-throughput benchmark or denial-of-service guarantee.
- Hosted Linux: existing real Telnet/PTY fixture, two distinct slot UIDs,
  persistent game saves, NAWS, game-server restart, parent stop, deadline,
  repeated parent SIGKILL and worker SIGKILL with retained BUSY.
- Hosted Rocky policy container: compile the actual policy; inspect the compiled
  allowed/denied graph; install/reinstall/update/remove RPM and preserve local
  label definitions. This does **not** count as Enforcing runtime proof.
- Trusted-main Rocky native: same existing integration scenarios, but Broker,
  isolation, BBS and consumer start via actual systemd with checked process
  domains. Game asserts its Door domain and rejection of a deliberately
  DAC-readable BBS-labelled synthetic file. The unit adapter retains shipped
  preflight, capability and stop behavior, translating only fixture identities,
  paths and dependency names. All test units have a 300-second runtime watchdog.
  No existing BBS service is changed. Test units and port mappings have unique
  names recorded in the root-only fixture record.
  Broker, consumer and BBS each use a temporary locked/non-login NSS account
  with a distinct primary group; Broker also uses the consumer supplementary
  group. Numeric UIDs alone are insufficient for systemd credential setup
  (observed `217/USER` before exec). Door slot UIDs remain unregistered because
  the isolation launcher drops their numeric credentials directly.
  All fixture IDs and generated names must be unused; existing identities are
  never reused or changed. No home directory is created. The native fixture
  records both creation intent (`door_account_plan`) and created identities
  (`door_accounts`) in `fixture-state.json`, stops its units first,
  and removes only unchanged identities after verifying no process references
  their UIDs/GIDs. Changed entries or live tasks cause a retained-state failure.
  Hosted Rocky checks real NSS creation, group assignment and removal inside
  its disposable container; this is not systemd/Enforcing runtime evidence.
  On startup failure, including `ExecStartPre`, the adapter captures at most
  40 journal lines/16 KiB from that unique unit before unit-file removal. A
  journal failure is recorded without replacing the original startup error.
  These diagnostics flow into `acceptance.log` and the native CI Artifact.

Native helper cleanup now recognizes all three new domains. Therefore the
installed root-owned `liner-notes-ci-selinux` hash must be refreshed to the
reviewed main version **before** native CI. Existing initial-setup permission
does not authorize this replacement automatically; request the limited helper
update after Hosted checks/main integration. Keep the workflow's hash check.

In interrupted native tests retain `fixture-state.json`, `acceptance.log` and
the exact SHA. Stop only recorded units, confirm all test tasks are gone, then
remove only their unchanged runtime unit files and port mappings. If temporary
`door_accounts` remain, verify their recorded names/IDs and non-login properties,
absence of foreign group members and primary-group references, and absence of
processes using any fixture UID/GID before removing only those entries. Never
force removal or remove a home directory. Do not unload
the policy while any candidate domain survives. Independent security review,
Solaris isolation and a production game-server policy remain separate gates;
no local portable test or earlier commit's CI certifies them.

## PR 142 manual/source review

Reviewed `ci/selinux_native.py` against `docs/manual/security.md`: the change
adds `door_accounts` and `door_account_plan` to the recovery record. The guarded
main/Enforcing admission, service permissions and policy removal checks are
unchanged. The manual now distinguishes the original `liner_notes_ci` services
from temporary Broker NSS identities and describes safe interrupted cleanup and
bounded pre-cleanup unit journals. Only the reviewed native source fingerprint
is refreshed; this records documentation correspondence, not native acceptance.
The initial PR checks failed on that omitted source-lock refresh in manual/RPM/DEB.
The separate doctor failure was an Ubuntu mirror connection timeout while
fetching clang, before its functional tests; no application defect was inferred.

## Hosted download recovery (2026-11-20)

PR 142's later failures were external retrieval failures: the Ubuntu kernel.org
mirror failed to supply clang, and both EL9 jobs timed out during the upstream
lrzsz TLS handshake. Manual/RPM/DEB checks and the SELinux policy/NSS test passed.

The restricted hosted Ubuntu mirror helper now selects JAIST:
https://ftp.jaist.ac.jp/pub/Linux/ubuntu/ . Ubuntu Japanese Team lists JAIST and
Yamagata University as domestic archive mirrors:
https://www.ubuntulinux.jp/ubuntu/mirrors . A local HTTPS probe retrieved noble,
updates, security and backports InRelease files plus the failed clang package;
this does not establish performance or reachability from every hosted runner.
Suites, components, Signed-By and APT freshness/signature verification remain
unchanged. Only disposable GitHub-hosted Ubuntu 24.04 amd64 may use the helper.

The lrzsz builder preserves its existing URL, fixed digest and build/CRC patch.
Network failures and selected transient HTTP statuses retry at most three times
(30-second socket timeout, 2/5-second backoff); certificate/integrity/permanent
HTTP errors are not retried. Redirects must remain HTTPS. The new download-only
mode publishes verified bytes atomically without overwriting existing files.
Cache hits and --archive input are always rehashed; corrupt cache fails closed.
Linux compatibility and hosted SELinux restore an exact digest-keyed source-only
cache, verify it on the host, and pass it into the existing private build. No
compiled engine or test outcome is cached, and there are no broad restore keys.
If a cache is corrupt, investigate/remove that cache entry; do not change the
pinned digest to accept it. Exhausted upstream failures remain CI failures.

Reviewed build-lrzsz.py against the canonical Linux installation chapter and
updated the source-lock for that file only. FreeBSD/Linux/Solaris build commands,
archive entry validation, CRC patch and resume receipt behavior are preserved.
The new unit tests cover retry exhaustion, HTTP/certificate/hash/size refusal,
HTTPS downgrade rejection, and verified/corrupt cache handling.

## Native service readiness (2026-11-20)

At main `0a012617ae9c8ba91d85b36f1aaf09b618a0398e`, both Broker and isolation
started in their expected SELinux domains. The adapter then probed isolation.sock
before it existed (`FileNotFoundError`); Type=simple and process-label admission
are not socket readiness. The adapter now checks the service is still alive and
retries only ENOENT/ECONNREFUSED within five seconds. Socket owner/type and kernel
peer UID checks are unchanged; identity/permission failures and early exit fail
immediately. Startup diagnostics are captured on readiness failure. Tests cover
delayed creation/listen, deadline, exited process and refused identity/access.
No service startup timeout or SELinux permission is relaxed. Canonical service
security guidance remains unchanged; this is an internal fixture sequencing fix.

Separately, FreeBSD isolated upgrade stopped before execution because its
installed selinux_root.py import was the older 09b84e1b digest. With explicit
permission, only that helper was backed up and replaced by reviewed main bytes
(8150d379), preserving root:wheel/0644. Its failed acceptance job was rerun on
the same main SHA; the result is separate from Rocky readiness acceptance.

## Native consumer identity (2026-11-20)

At main `b02fbdf6fb51999e91adb9d739d5629d3f4a0ac2`, run
[44574999661](https://github.com/matsuotakeshi/liner-notes/actions/runs/44574999661)
passed policy compilation and progressed beyond Broker/isolation readiness.
The consumer unit then failed before exec with `Failed to determine user
credentials` / `217/USER` (Artifact 14556038443). The fixture had registered the
Broker user and consumer group but not the consumer user. Its later BBS unit
also lacked an NSS user and primary group.

The adapter now registers all three systemd service users and primary groups.
Collision checks cover all names and reserved IDs before changes. Recovery
intent and successful creation records include each account; cleanup checks
every created identity and foreign group reference before deleting any entry.
Tests cover partial user creation, additional name collisions, all service
credentials and preservation of the unregistered Door slots. The existing
Hosted Rocky container test checks real NSS/group initialization and UID/GID
drop for every service user before removing the accounts.

This fixes the test adapter only: no production accounts, service templates,
SELinux grants or privileged installed helper are changed. Hosted checks do
not replace trusted-main native Enforcing acceptance, which remains pending.


## Isolated launch diagnosis (2026-11-20, candidate only)

Main `4664068c6a7a83307aaa6515d352859ef24c1e0b`, native run
[44580196385](https://github.com/matsuotakeshi/liner-notes/actions/runs/44580196385),
Artifact 14559187298: all four service domains were entered. Broker/isolation
socket readiness and consumer/BBS NSS registration succeeded. Selecting game 1
returned `Game unavailable` before the authenticated game prompt. The captured
service journals and filtered AVC output did not identify a failed operation.
This is not evidence of a policy denial or proof that more startup time is needed.
The root cause remains open; this change adds evidence, not a claimed fix.

The opt-in `LINER_DOOR_DIAGNOSTICS=1` environment setting emits
`DOOR_LAUNCH_FAILURE phase=<fixed-label> errno=<number>` to runtime stderr.
Only the isolated acceptance BBS unit enables it automatically. Unset, `0`, and
other values remain silent. No path, account, member/session ID, request, ticket,
key or game data is logged. The helper preserves errno and existing user-facing
responses. Review the phase rather than treating errno 0 (validation failure or
non-OS error) as success. Labels distinguish session admission, identity/catalog
selection, socket/connect/peer, random/request encoding, sending, reply/descriptor
validation, PTY and nonblocking setup. Existing journal capture exports these
lines without adding access to host logs or running a second privileged probe.
Do not enable this optional diagnostic permanently on public servers without
considering repeated failed-launch log volume. No production setting is changed.

Local portable tests compile the C logger and check exact opt-in, output and errno
preservation. Linux/FreeBSD CI also compiles the actual isolation client and tests
a missing socket with diagnostics on/off; the Windows run marks that Unix case
not applicable. Existing native acceptance still requires the real authenticated
Door lifecycle to pass, not merely a new diagnostic line.

Reviewed `src/doors.c`, `src/runtime.c`, `src/door_isolation.inc` against the
canonical Door guidance: ACL, peer UID/FD validation, timeouts, replies and process
cleanup are unchanged. `Makefile` only adds the header dependency and focused test.
Their source-lock hashes are updated after this review. The stable public manual
remains unchanged; this candidate diagnostic is documented here until release.


## Worker exception evidence (2026-11-20, candidate only)

At main `985b9a8f2ed54f4e11a14d199df72d5a876e6dcd`, native run
[44588358780](https://github.com/matsuotakeshi/liner-notes/actions/runs/44588358780),
Artifact 14564151540, the C client reports `isolation-rejected errno=0`.
It connected, verified the root peer, sent the request and received three bytes
that were not `OK\n`. This does not distinguish `ERR` from `DON` (the first
three bytes of cleanup's `DONE\n`), or prove a SELinux denial.

Inspection found that `start_worker` used `finally: os._exit(0)`, suppressing
unexpected exceptions and normal traceback reporting. The existing supervisor
only logged selected exception types. The candidate now records unexpected
launch failures before cleanup, cleanup failures at their own phase, and exits
unexpectedly failed workers with status 1. The parent reports nonzero worker
wait status. Successful workers retain status 0; ordinary handled refusals
retain their existing ERR/DONE and cleanup behavior. This closes an evidence
gap; it does not claim to fix the unknown native game-launch failure.

Events: `launch-rejected` (handled validation/OS error), `launch-failed`
(unexpected launch exception), `cleanup-failed`, `worker-failed`, and
`worker-exit`. Fields contain fixed phases, exception class, numeric errno or
wait status only. No exception message, traceback, request, ticket, path,
member/session identifier or local variable is included. New phases distinguish
scope setup, broker request, ticket pipe, dropfile, PTY, handoff and runtime;
cleanup distinguishes revocation, descriptors, scope stop, dropfile removal,
slot release and connection close. Failed stderr output cannot interrupt the
ERR response or cleanup. Failed scope cleanup still retains the slot reservation.

Fault-injection tests run without forking, signaling real processes or changing
privileges. Windows substitutes Unix-only import modules; those control-flow
checks are not evidence of native privilege isolation. The same tests run in
Hosted SELinux CI and `make check`; real Linux/FreeBSD lifecycle checks remain.
Do not run the candidate on home VMs until Hosted CI passes and it is merged
into trusted main. The existing main push workflow then supplies the next
Enforcing evidence without a privileged helper update or extra root login.

Reviewed the canonical Door recovery guidance against `scripts/door_isolation.py`:
revocation precedes cleanup, reservation release remains after successful scope
cleanup, and authentication/ACL/timeouts are unchanged. Updated its source-lock
and the Makefile lock (one focused test added). Public stable documentation
remains unchanged; the new diagnostic is a candidate behavior described here.
