# Door authorization Broker and isolation candidate

This is an opt-in development candidate, not a released or production-validated
feature. Do not enable it before the hosted privileged integration test and an
independent security review pass. No service is installed/enabled automatically.

## Trust boundary and threat model

The previous same-UID Door can read BBS-owned credentials/files and impersonate
BBS requests. A signature or separate Broker alone would not fix that. Enabling
`--door-isolation-socket /absolute/socket` routes **every** Door through a local
privileged supervisor. Failure never falls back to the old launch path.

- BBS: existing unprivileged service UID; authenticated Core supplies member ID,
  level, connection ID and authentication epoch. No client-selected identity.
- Isolation supervisor: root, local AF_UNIX only, pinned root-owned policy and
  binaries. Accepts the configured BBS UID through kernel peer credentials.
- Door: distinct UID/GID per concurrent slot; no supplementary groups; Linux
  no_new_privs; closed inherited FDs except terminal and optional ticket FD 3;
  bounded CPU/time/files and no core dump. Never reuse these UIDs for a service.
- Broker: separate unprivileged UID; accepts issuing requests only from the
  privileged supervisor. BBS and game UIDs cannot issue. No network listener.
- Game server: dedicated consumer UID, explicitly mapped to allowed Door IDs;
  different from all Door slots/BBS/Broker. It consumes and rechecks leases.

The root supervisor is privileged trusted code; compromising it is outside the
unprivileged Door threat boundary. Root-owned configs/executables and safe
ancestors are mandatory. Existing BBS-UID games must all be stopped before
opting in; never run another legacy BBS/launcher under that UID concurrently.
The policy is not a filesystem/network sandbox: a Door can still access files
which the administrator makes public or writable to its slot UID. Keep BBS,
Broker, game-server data and other slots private. Slot UID/GID numbers must be
dedicated and distinct from the service accounts and privileged groups.

## Protocol and lifecycle

Issuer sends one bounded JSON line with `op=open`, member, Door ID, session and
generation. Session contains a random launch nonce plus the Core connection ID;
generation is the Core authentication epoch, including valid initial epoch 0. The issuer connection remains open
for the complete game lifetime. Broker returns a 256-bit random, 30-second
one-use ticket. Only its SHA-256 lookup key is kept internally. The supervisor
passes the ticket through FD 3; `LINER_AUTH_FD=3` contains no credential.
No password, signing secret, token or lease is placed in command arguments/logs.

The game-server consumer uses `door_auth_client.request(path, 'consume', bbs=...,
door=..., token=..., broker_uid=...)` under its registered UID. It obtains bound claims and a
random lease. Claims include BBS/member/Door/session/generation/ticket identity
and remaining validity. The client checks the Broker socket peer UID before sending credentials.
`DoorAuthorization.authorize(member)` rechecks the lease and rejects a different
member before a game mutation. The game server must derive player identity from these
claims, reject operations targeting another member, and call `check` before
every authoritative mutation. Never trust a Door-provided member ID or cache a
successful check indefinitely. Protect the Door-to-game-server channel too.
The BBS Broker protocol itself is local; remote consumer authentication is not
implemented.

Reusing a ticket, changing BBS/Door, using another consumer UID, forged tokens,
old generations, closed issuer connections and expired sessions are rejected.
Old-session close cannot invalidate a replacement session. Broker restart drops
all tickets and leases. SIGTERM removes its socket on a clean shutdown; an
unclean leftover socket is never silently removed on startup. Core dumps are
disabled in both services. Existing game data is outside the Broker and untouched.
Broker death makes the supervisor stop the game. The game server must fail
closed if it cannot reach the Broker, including for already consumed tickets.

Linux isolation retains a slot until all tasks under its dedicated UID have
exited. pidfds avoid recycled-PID signalling; HUP is followed by KILL after a
short grace. Double-fork/setsid descendants are included. Failed cleanup keeps
the slot unavailable; the supervisor must not be forcibly killed and its UID
recycled. The socket lifecycle controls revoke/stop; BBS does not signal a
foreign-UID child directly. The PTY master is passed using SCM_RIGHTS, so existing
terminal I/O and NAWS resize use the same PTY API.

Reference APIs: [Linux subreaper](https://man7.org/linux/man-pages/man2/PR_SET_CHILD_SUBREAPER.2const.html),
[FreeBSD reaper](https://man.freebsd.org/cgi/man.cgi?query=procctl).
The FreeBSD candidate now uses its own reaper/no-new-privileges implementation,
with native own-descendant tests in `make check` and the privileged integration
in the existing trusted-main disposable package-upgrade jail. Neither is a
FreeBSD success claim until that exact SHA passes. PID enumeration is used only
for refusing already-occupied UIDs; signalling uses the kernel reaper subtree.
Solaris peer identity uses `getpeerucred`/`ucred_geteuid`, with native socket checks
in `make check`. Its launcher still rejects activation: process contracts and
privilege-limit handling are not implemented. Do not substitute killpg or a
same-UID launcher. See [Oracle peer credentials](https://docs.oracle.com/cd/E88353_01/html/E37843/getpeerucred-3c.html).

## Example policies (development only)

Broker JSON, root-owned readable file; socket directory owned by Broker, not
writable by other users. Its socket group allows the game server; Broker must
be a member of that group to create the socket with that GID.

```json
{
  "bbs": "my-bbs", "bbs_uid": 65001, "broker_uid": 65002,
  "socket": "/run/liner-notes-door-broker/broker.sock", "socket_gid": 65003,
  "door_uids": [65004, 65005], "consumers": {"65003": [1]}
}
```

Isolation JSON, root-owned. The second slot follows the same pattern with node
2 and UID/GID 65005. The complete UID set must match the Broker policy.

```json
{
  "bbs_uid": 65001, "socket": "/run/liner-notes-door-isolation/control.sock",
  "socket_gid": 65001, "broker_config": "/etc/liner-notes/door-broker.json",
  "state_dir": "/var/lib/liner-notes-door-isolation",
  "slots": [
    {"door": 1, "node": 1, "uid": 65004, "gid": 65004,
     "exe": "/usr/local/libexec/my-lobby-wrapper", "cwd": "/var/lib/liner-notes-door-slots/slot1",
     "level": 0, "seconds": 3600, "drop": true, "auth": true},
    {"door": 1, "node": 2, "uid": 65005, "gid": 65005,
     "exe": "/usr/local/libexec/my-lobby-wrapper", "cwd": "/var/lib/liner-notes-door-slots/slot2",
     "level": 0, "seconds": 3600, "drop": true, "auth": true}
  ]
}
```

Keep root-owned state directory searchable (0711) so each slot can reach its
private 0700 drop directory. Locks and dropfiles are 0600. The wrapper must live
outside writable game data. Root policy is authoritative for executable, work
directory, minimum level, duration, slots and dropfile. Keep the BBS doors.conf
listing consistent; the supervisor accepts no client-supplied path. `auth=false`
is for an existing unauthenticated game, but it still runs under an isolated UID.
DORINFO1.DEF, LINER_USER_ID/USER/DOOR_ID/DOOR_NODE, PTY and game-menu return remain.

The candidate scripts are invoked explicitly with `--config`; package copying
does not provision accounts or start daemons. Linux installs two reviewable
systemd examples under `share/liner-notes/door-services/`, outside the systemd
unit search path. They are syntax-checked in hosted CI, not boot-tested by it.
The Broker uses `liner_notes_broker` with supplementary `liner_notes_games`;
the launcher runs as root with a bounded capability set, no-new-privileges and
write access to `/var/lib/liner-notes-door-slots`. Numeric JSON identities must
match provisioned users/groups; examples do not create these or per-slot folders.
The launcher state must be `/var/lib/liner-notes-door-isolation` (0711), with
its socket in `/run/liner-notes-door-isolation`. Broker socket belongs in
`/run/liner-notes-door-broker`. Configure BBS socket group as `liner_notes` and
Broker socket group as the consumer group's actual GID. Do not reuse slot groups
for BBS, Broker or game-server data.

Before any future service activation, verify accounts, fixed wrapper paths,
private data permissions, local policy and SELinux labels. The Linux service and
SELinux candidate is described in [Linux operation and recovery](DOOR_LINUX_OPERATIONS.md);
its new native tests are not success evidence until the exact main SHA passes.
Solaris isolation and SMF lifecycle remain unimplemented.
FreeBSD rc samples and a jail lifecycle test are added below; their native
acceptance is pending for this candidate. This is **not a production runbook**.

### FreeBSD service candidate and preflight

The port installs `liner_notes_door_broker` and `liner_notes_door_isolation`
under `share/liner-notes/door-services/freebsd/` and registers the executable
samples under `etc/rc.d/` when no local service definition exists. No automatic
activation, policy creation, account provisioning or directory repair is done.
For an independently provisioned development instance only, root can invoke
these samples through `/bin/sh SAMPLE onestart|onestatus|onestop`.
The default `_enable` setting is `NO`; normal `start` stays disabled.

Each script reads the following rc.conf settings with its own name prefix
(`liner_notes_door_broker` or `liner_notes_door_isolation`):

| Suffix | Default | Requirement |
| --- | --- | --- |
| `_enable` | `NO` | Explicit activation only after acceptance/review |
| `_config` | `PREFIX/etc/liner-notes/door-broker.json` or `door-isolation.json` | Existing root-owned policy with safe ancestors |
| `_pidfile` | `/var/run/NAME.pid` | Root-controlled path; daemon owns the child PID file |
| `_logfile` | `/var/log/NAME.log` | Root-controlled path; created under umask 077 |
| `_run_user` | `liner_notes_broker` or `root` | Broker account matches policy, with consumer socket group membership; supervisor requires root |

Provision persistent supervisor state (0711, root-owned), private slot work
directories and dedicated accounts according to the policy above. On FreeBSD,
use `/var/run/` for sockets and `/var/db/` for persistent state instead of Linux
`/run/` and `/var/lib/`. Keep wrapper executables outside writable game data.
Start Broker first, then isolation, then the opted-in BBS; stop in reverse order.
The REQUIRE/BEFORE markers describe boot ordering only, not automatic dependency
startup for a manually invoked script. Do not run legacy BBS-UID Doors alongside.

`door_broker.py --config FILE --check-config` and the isolation counterpart
parse/validate policy without binding sockets or creating state. Isolation
checks require root and a supported backend. `--check-ready` additionally opens
the configured local socket and verifies its file type, owner and kernel peer
UID, without sending authentication data. It is an intentional local probe,
not a fully offline diagnostic. It does not attest every possible game launch.
The Linux systemd samples use configuration checks as ExecStartPre too; Linux
boot/service validation remains a separate gate.

The service-template regression permits exactly the read-only `--check-config`
preflight and normal start command. Other Exec hooks (including reservation
reset/cleanup) remain rejected. The preflight tests separately verify that the
check does not start a server, bind a socket or fork a worker.

Start refuses a duplicate process and reports readiness only after both child
PID and socket identity checks succeed. Stop checks the child PID, UID and service arguments before TERM and waits for the daemon to release the PID file.
The samples do not use automatic restart, SIGKILL, socket unlink, or reservation
reset. A failed start/stop requires inspection; never clear BUSY as a workaround.
An orphan socket is not ready and is not automatically replaced.
Reference: [FreeBSD daemon](https://man.freebsd.org/cgi/man.cgi?query=daemon&sektion=8)
and [rc.subr](https://man.freebsd.org/cgi/man.cgi?query=rc.subr&sektion=8).

The trusted-main disposable jail creates only its own fixture accounts and
executes the installed samples. The existing two-game/PTY/crash fixture now
uses rc-managed services on FreeBSD, including explicit stop with descendants,
status after stop, duplicate refusal and restart. Linux retains its existing
container process fixture. Windows local tests cover portable preflight/readiness
contracts; shell/native service checks require the corresponding CI. Passing
the earlier raw-process fixture is not evidence that these new service tests passed.

### Stop and crash reservations

The FreeBSD rc fixture at main `d60f46dc819813526129b92b3a1ed82a01c7850d`
failed final cleanup in [run 44539986417](https://github.com/matsuotakeshi/liner-notes/actions/runs/44539986417).
The private failure log identifies `RcProcess.wait`: an earlier supervisor had
already completed its stop and readiness checks, but final cleanup repeated its
status check after another generation had started under the same rc name.
That newer live service caused `stopped rc service still ready`.

The fixture adapter now caches a completed wait only after child exit, PID-file
release, successful requested stop and a negative service-readiness result.
Later waits on that object return its own completed result, as `Popen.wait` does;
they do not inspect or stop a newer generation. First-wait readiness/stop failures
and pending PID-file cleanup remain failures/timeouts. The adapter lives in
`tests/door_rc_process.py` so portable tests can reproduce the old failure without
starting services or importing Unix-only integration code. The four added tests
run through the existing `test-door-broker` target.

This is a fixture correction, not a change to installed rc scripts, Broker,
launcher or BBS behavior. Reviewed the stable Door/security chapters: no user
operation changes. Local reproduction fails on the old adapter and passes with
this correction. Native acceptance still requires the corrected trusted-main
CI; the previous failure is not relabeled as success.

Each worker watches a parent-owned pipe. Parent termination, including SIGKILL,
closes that pipe; workers revoke the issuer connection and clean up their Door
descendants. The normal parent stop waits for workers. No forced worker kill is
used as a successful cleanup shortcut. The systemd example uses `KillMode=process`,
`SendSIGKILL=no`, an infinite stop timeout and no automatic restart for this reason.

A root-owned slot lock also contains a persistent BUSY marker, written/fsynced
before launch and cleared only after proven cleanup. Abnormal worker death or a
cleanup API failure leaves the marker; a restart refuses that slot even if flock
is no longer held. Never delete/recreate lock files while workers can exist.
An administrator must establish all services/worker descendants are stopped and
the dedicated UID has no processes before clearing a retained reservation through
the same locked inode. Automated recovery/reset is intentionally absent. Prefer
a persistent state directory; do not let service cleanup erase it while children
may survive. After a complete host reboot no old processes survive, but markers
still require an explicit review rather than silent reset.

## Validation and incomplete scope

Portable Core/protocol tests cover identity, replay, expiry, two users, generation,
revocation, capacity, malformed input and denied issuer UIDs. Hosted Linux
container integration additionally creates private numeric UID fixtures, runs
real Broker/supervisor/BBS processes and Telnet games, checks independent player
identity, private dropfiles, leases, detached children and no legacy fallback.
It never runs on private CI hosts and does not provision production accounts.

Added integration assertions: NAWS reaches the isolated PTY, every mutation
rechecks member/lease authority, replay and other-member operations are rejected,
game-server restart preserves committed SQLite data, parent service stop removes
detached children, and stale BUSY markers prevent reuse. These are a synthetic
game fixture, **not AREA-PC88 acceptance**. See [AREA-PC88 handoff](DOOR_AREA_PC88_HANDOFF.md).

Baseline main `ed3adacdb7771f4db4bcd342c1cd7d184506dca5` passed
[FreeBSD jail acceptance](https://github.com/matsuotakeshi/liner-notes/actions/runs/44514710751)
(artifact `14524706953`) and
[Linux compatibility](https://github.com/matsuotakeshi/liner-notes/actions/runs/44512852526).
These cover isolated launch and the crash lifecycle fixture below, not future
changes or OS service activation. Private AREA-PC88 integration also passed for
its pinned candidate combination; private evidence is retained separately, not
published or generalized to later builds.

Still required: Solaris process contracts/privilege backend, SELinux policy,
complete OS service lifecycle tests and independent security review. Broader
resource-exhaustion and timeout stress are not complete. User manual acceptance
is not used to substitute for these development tests. Do not mark LN-38 complete
or deploy this candidate yet.

### Crash lifecycle regression expansion

The existing privileged fixture now additionally asserts:

- A short supervisor policy deadline revokes authority and stops detached games,
  even when the BBS listing has a longer time limit.
- Two successive parent SIGKILL/restart cycles close the inherited watch pipe,
  revoke leases, reclaim descendants and preserve committed synthetic game data.
- SIGKILL of a cleanup worker revokes its issuer connection but retains BUSY.
  A deliberately detached child remains possible: the slot must be rejected,
  not silently marked clean. The test's outer reaper then removes only fixture
  descendants, and the BUSY evidence must still remain.
- Readiness means both no slot processes and an unlocked, empty reservation.
  Process exit alone is earlier than the reaper grace period and is insufficient.

An orphan supervisor socket is removed only in the disposable fixture, after
parent death, verified child cleanup, socket-type and inode checks. This is not
an automatic production recovery implementation. The manually inserted dirty
marker from the older test is cleared under its existing lock before the real
crash scenario; a real crashed worker's marker is never cleared.

Portable protocol tests cover oversized/truncated/non-object requests, capacity
recovery on issuer disconnect and consumed-lease revocation on task cancellation.
The issuer test waits for the actual blocked reader rather than counting event
loop ticks. Local Windows execution covers these portable tests only; Unix peer
and FreeBSD reaper checks are explicitly platform-specific.

No new CI environment is introduced: `ci/linux.sh` runs the expanded fixture in
Hosted Linux containers, and `ci/freebsd_upgrade_jail.py` runs it in the existing
trusted-main disposable jail. Added assertions are not OS success evidence until
the corresponding exact-SHA CI passes. Broader resource-exhaustion, service,
Solaris isolation and independent security review remain outstanding. These
synthetic tests do not publish or replace private game-specific evidence.

Manual review: `docs/manual/external-games.md` and `docs/manual/security.md`
retain the distinction between stable legacy Doors and opt-in development
isolation. This test-only change does not alter commands, ACL, activation,
configuration or recovery policy; public instructions require no behavior edit.

### FreeBSD UID inventory correction

Main `f91a5ef` failed run `44495450243` before the first isolated Door launch,
inside `processes(uid)`. The jail receipt says `link` because the Broker test
shares that phase; it was not a failure of BBS link synchronization. The private
failure log identifies `Cannot establish slot UID availability`.

FreeBSD's [ps format parser](https://github.com/freebsd/freebsd-src/blob/releng/15.0/bin/ps/keyword.c#L395-L405)
treats the rest of an argument after `=` as the column header. Consequently
`-o uid=,ruid=,svuid=` does not request three headerless UID columns. Use repeated
`-o uid= -o ruid= -o svuid=`. Keep strict three-column numeric parsing and refusal
on missing/malformed output; do not skip unknown rows or assume an empty slot.

The regression tests retain rejection of the old single-column/header output
and add a native, unprivileged self-UID inventory check to `make check`. The
existing privileged jail fixture still has to pass after trusted-main integration.
This correction does not change permissions, process signalling or activation
policy. Stable user instructions remain unchanged; no production host was used.

### Restrictive-umask fixture correction

At main `57f03cca78ca98a0cdab840130169caef7c98184`, acceptance run
`44500498907` passed UID discovery but failed before Broker startup:
Python could not open the fixture's `scripts/door_broker.py` (permission denied).
The root jail helper sets umask `077`, inherited by the fixture. `mkdir(mode=...)`
is still filtered by that mask; the scripts directory was consequently private.
The same issue also affects cross-UID traversal of socket/run directories and
reading generated, non-secret fixture policy.

The fixture now applies explicit modes only to shared code, public policy and
required traversal directories. It retains private BBS data, state files,
per-run directories, tickets and game saves. No recursive permission change or
process-wide mask relaxation is used. The existing Linux root fixture runs with
umask `077` too, so Hosted CI exercises the same access constraints before merge.
No extra test matrix or weakened assertion is introduced. FreeBSD jail acceptance
still requires trusted-main integration; this change alone is not success evidence.

Reviewed `docs/manual/external-games.md` and `docs/manual/security.md`: this is
a test fixture correction, with no product command, permission or setup change.
The stable manual remains unchanged.

### Admission and communication resource limits

The next candidate bounds isolation request reads by one three-second monotonic
deadline, including partial/trickled input. Broker-response reads use the same
rule. The game-server consumer's existing `timeout` argument (default three
seconds) now covers connect, send and the complete response, rather than each
individual byte. Protocol fields, identities and policy JSON are unchanged.

Broker output drain and close waits are each bounded to three seconds. A failed
issuer response revokes the session before attempting the denial reply; a stuck
close aborts that transport. No ticket/request content appears in diagnostics.
This bounds a handler's waits; it is not a claim of resistance to arbitrary
host-wide memory exhaustion or a compromised privileged supervisor.

The isolation supervisor rejects new requests on peer-credential or fork failure
without closing the watch pipes of running Doors. Transient accept resource
errors (EMFILE, ENFILE, ENOBUFS, ENOMEM) use a 100-ms backoff; unrelated errors
remain fatal. Existing worker count limits, UID checks, persistent BUSY markers
and cleanup proof remain mandatory. No failure silently falls back to same-UID
execution or clears a reservation.

`test_door_isolation_admission.py`, included in `make check`, injects resource
errors without exhausting the CI host, verifies preserved worker inventory and
subsequent admission, and tests an absolute request deadline. Portable Broker
tests verify a stalled writer/close, consumed-lease revocation, recovery and a
trickled consumer response. The privileged Linux/FreeBSD fixture now sends a
partial request from the BBS UID while two games are active, requires rejection
within a bounded wait and checks both existing leases remain active. Unix-only
tests are skipped explicitly on Windows. Native results for this change are
pending the normal PR/main CI sequence, not inherited from the baseline above.

Reviewed the stable Door and security manual chapters. Only the development
status summary changes; no released configuration or HELP operation is added.

## Scoped launch replay protection (candidate, 2026-11-20)

The current isolation supervisor uses root-only `open_scoped` with `scope` and
`sequence` in addition to the existing `member`, `door`, `session`, `generation`.
Scope is a random supervisor incarnation plus the dedicated slot UID; sequence
is assigned monotonically by the single-threaded supervisor parent before fork.
Neither field is accepted from the BBS request. Failed forks leave gaps. Original
BBS session identity and authentication generation remain in game-facing claims.
A newer launch must not collide with an active slot; an old close object cannot
revoke a replacement. Expired records keep the same session-TTL rule.

Replay history is now bounded by incarnation/slot count, not every launch's
random session ID. Repeated launch/quit no longer consumes global history for
ordinary users. Rapid supervisor restarts still retain old scopes until TTL, and
admission/active sessions remain bounded; this is not unlimited capacity. Workers
which arrive out of sequence may be safely rejected rather than reusing a stale
sequence. No PID, session ID, member ID or saved game is renumbered.

Upgrade Broker before supervisor, with the existing stop/revoke/restart procedure
and matching package versions. The new Broker accepts legacy `open` unchanged;
legacy supervisors retain the old per-session history limitation. A new
supervisor talking to an old Broker is rejected, never downgraded silently.
Public consumer `consume`/`check` and claims are unchanged. No service is updated
by the development tests. See the independent review follow-up.
