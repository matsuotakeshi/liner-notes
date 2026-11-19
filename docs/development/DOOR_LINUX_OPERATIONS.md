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
`liner_notes_isolation_t`, and `liner_notes_consumer_t`. Existing BBS and Door
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
domain. Broker and consumer have no capabilities or general TCP rule. A remote
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

Native helper cleanup now recognizes all three new domains. Therefore the
installed root-owned `liner-notes-ci-selinux` hash must be refreshed to the
reviewed main version **before** native CI. Existing initial-setup permission
does not authorize this replacement automatically; request the limited helper
update after Hosted checks/main integration. Keep the workflow's hash check.

In interrupted native tests retain `fixture-state.json`, `acceptance.log` and
the exact SHA. Stop only recorded units, confirm all test tasks are gone, then
remove only their unchanged runtime unit files and port mappings. Do not unload
the policy while any candidate domain survives. Independent security review,
Solaris isolation and a production game-server policy remain separate gates;
no local portable test or earlier commit's CI certifies them.
