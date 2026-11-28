# LN-38-C / D production design

Fixed-slot configuration requires one distinct UID/GID and one working directory
for each configured (game, node) tuple. The isolation configuration supports
up to 128 entries. The catalog's configured game capacities are not a measurement
of site-wide simultaneous demand. The site inventory has 22 games configured
with 32 nodes each (704 logical nodes), plus other entries. Preserve these
catalog values until a site-wide concurrency policy has been explicitly agreed.

## Compatibility boundary

The BBS-wide isolation switch remains fail-closed. Every enabled Door needs an
isolated execution policy before switching the instance. `auth=false` remains
a separate property for legacy applications and suppresses ticket issuance.
`auth=true` requires the existing FD and Broker checks. The BBS never launches
a program with its own UID as a fallback. No consumer is granted direct access
to a slot's files. Dropped supplementary groups remain mandatory.

For each of the 25 configured Doors, record the executable and interpreter,
working directory, persistent versus temporary files, file-lock scope,
drop-file behavior, get/save/reconnect workflow, expected node number,
device and network access, and whether another process owns shared state.
Inventory must be sanitized and collected without changing the live host.
Where there is no fixture or redistribution permission, record the case as
requiring operator-run acceptance with a private copy. The following are
migration tracks to validate, not assertions about the site's game installs:

| Track | Required acceptance |
| --- | --- |
| Standalone game with private saves | Same member reconnects to the expected save; concurrent slots cannot access another slot's state |
| DORINFO1.DEF consumer | Correct node, member, expiry and private per-launch drop directory; stale drop files are inaccessible |
| Legacy game with shared files/locks | Verify shared-lock semantics and establish a game-specific narrow data interface or a reviewed per-game ACL; maintain the exact save format |
| Game server plus lobby | Fixed client executable, only the allotted IPC socket, durable DB owned by server only; no credentials or DB in the slot |
| Network or PTY game | Server peer verification, no broadened access to other services, PTY and resize behavior |

AREA-PC88 standalone Door 21 and planned Broker lobby Door 26 have separate
policies. The latter must use the existing server-owned world DB. The lobby's
per-slot UID needs only traversal and access to that server's IPC, never the
DB directory. Because launcher supplementary groups are removed, group-only
permission is insufficient unless the slot actually has that primary group.
Validate the filesystem's ACL behavior across daemon restart in a disposable
FreeBSD environment before choosing an IPC access mechanism. Keep the BBS
identifier equal to the existing world's binding. No test should overwrite
that binding or reuse the production DB. The pinned AREA revision and current
main both expose the lobby Broker flags and consumer entry points; private
integration CI already exists. Track AREA's service work (PR #8) and Broker
audit (PR #11) upstream rather than copying private code into this repository.

## Capacity alternatives

| Model | Benefit | Cost / constraint |
| --- | --- | --- |
| Fixed one UID per logical node (current) | Simple ownership, revocation and forensic model | More than 700 accounts/entries for the stated catalog, above the current parser bound |
| Per-game dedicated UID pool, separate logical node numbers | Reuse identities only within one game's ownership boundary; provision for observed concurrent work | Requires atomic global/game reservation and an explicit migration for games that bind saves or locks to a node |
| Cross-game dynamic UID pool | Fewest identities and flexible peak assignment | A UID may retain ownership or permissions across games; process cleanup alone cannot prove filesystem isolation |

Recommend the per-game pool for first implementation. Hold shared immutable
assets root-owned and keep slot-local writes private. Games requiring
node-persistent files must be pinned to an independently reviewed policy until
their migration or game-owned server interface is proven. Do not implement
cross-game UID reuse in this phase.

Map game-specific logical nodes independently of OS identities, with the
existing catalog limits as per-game caps. A separately configured global cap
would limit all games in aggregate; size it from measured or explicitly agreed
demand. Never silently reduce a catalog limit or silently choose a global cap.
If operators require every game's configured maximum simultaneously, a smaller
pool cannot satisfy that requirement. Start with reporting and explicit
configuration validation before enabling admission.

Acquire the global and game/node reservations in one ordered transaction and
bind them to a supervisor incarnation and launch sequence. Revoke the lease on
disconnect and keep the reservation busy until the kernel process scope is
empty, UID ownership is verified, and launch files have been removed. A stale
socket, failed cleanup, or surviving UID holds capacity unavailable for operator
inspection. Persist crash markers under the existing root-owned state boundary.
Never clear them on startup or reuse an identity across games. Reuse the
already-reviewed replay watermarks without weakening expiration or caller
checks.

Review gates before implementation: explicit global-cap semantics, game-local
identity reuse with no cross-game assignments, mapping for persistent
DORINFO/node state, and the FreeBSD ACL/mechanism for the game IPC. Prove
process cleanup and concurrent capacity on trusted-main lab hosts. Exercise
previous and next launch generations, capacity exhaustion, dropped privileges,
consumer and non-consumer identity separation, storage access, and rollback.
The 2026-11-20 independent review accepted the scoped replay-watermark fix in
its review scope; its native and real-game qualifications still apply.

## Approved per-game policy

The operator selected dedicated per-game pools and deferred concurrency tuning.
Keep existing fixed entries in `slots`. A game appears in either `slots` or
`pools`, with one policy per pooled game. The following is a lab example of
the additional fields, using synthetic identities and an illustrative node
count.

```json
{
  "slots": [],
  "pools": [
    {
      "door": 7, "nodes": 32, "level": 0, "seconds": 30,
      "auth": true, "drop": false,
      "exe": "/usr/local/libexec/example-lobby",
      "cwd": "/var/db/example-lobby/assets",
      "accounts": [{"uid": 7001, "gid": 7001}, {"uid": 7002, "gid": 7002}]
    }
  ]
}
```

Set `nodes` to that game's configured logical node count. The game receives a
free node through `LINER_DOOR_NODE`. Two accounts in this example can run two
simultaneous instances. The 128-account validation bound includes all fixed
slots and all pool members. Broker `door_uids` must match that full set.
The optional top-level `max_active` caps simultaneous reservations across
fixed and pooled games, including quarantined reservations. With the field
omitted, no separate site-wide cap applies. Exhaustion rejects admission
without falling back to the BBS UID or changing `doors.conf`.

Choose dedicated identities with no previous game ownership when provisioning
a pool. The supervisor persists the game assignment in `pool-uid-<uid>` and
takes one lifetime lock for each configured state directory. Keep the
root-owned state directory on persistent local storage. Preserve its assignment
and reservation records during upgrades and rollback. Keep retired pooled
accounts reserved to the same game. A missing ledger after state loss must be
reconciled against provisioning records before starting the service.

The working directory and its ancestors must be root-owned. Pooled execution
gets a private per-launch `HOME` and `TMPDIR`. The supervisor removes the
standard drop file after process cleanup. Unexpected files or surviving
processes keep the UID and logical node reserved for inspection. A root-owned
working directory and a private temporary directory cannot restrict an
application that writes to other writable paths. Enable a pool only for a
reviewed launcher whose persistent state is held by a separately identified
server, or whose writable paths, ownership and cleanup were validated
independently. Keep a fixed-slot policy for games tied to persistent node state
until the game-specific migration has been tested.

Before production rollout, confirm FreeBSD process cleanup, concurrent
admission, IPC permissions across daemon restart and login with private
AREA-PC88 fixtures. Record the legacy games that still require acceptance.
