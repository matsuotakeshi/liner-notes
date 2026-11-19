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
The FreeBSD API needs a separate implementation and native rejection/cleanup
verification; it is not substituted with a Linux assumption.

## Example policies (development only)

Broker JSON, root-owned readable file; socket directory owned by Broker, not
writable by other users. Its socket group allows the game server; Broker must
be a member of that group to create the socket with that GID.

```json
{
  "bbs": "my-bbs", "bbs_uid": 65001, "broker_uid": 65002,
  "socket": "/run/liner-broker/broker.sock", "socket_gid": 65003,
  "door_uids": [65004, 65005], "consumers": {"65003": [1]}
}
```

Isolation JSON, root-owned. The second slot follows the same pattern with node
2 and UID/GID 65005. The complete UID set must match the Broker policy.

```json
{
  "bbs_uid": 65001, "socket": "/run/liner-isolation/control.sock",
  "socket_gid": 65001, "broker_config": "/etc/liner-notes/broker.json",
  "state_dir": "/run/liner-isolation/state",
  "slots": [
    {"door": 1, "node": 1, "uid": 65004, "gid": 65004,
     "exe": "/usr/local/libexec/my-lobby-wrapper", "cwd": "/var/lib/my-lobby/slot1",
     "level": 0, "seconds": 3600, "drop": true, "auth": true},
    {"door": 1, "node": 2, "uid": 65005, "gid": 65005,
     "exe": "/usr/local/libexec/my-lobby-wrapper", "cwd": "/var/lib/my-lobby/slot2",
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
does not provision accounts or start daemons. Linux systemd/SELinux units and
FreeBSD/Solaris supervisors are pending; this is **not a production runbook**.

## Validation and incomplete scope

Portable Core/protocol tests cover identity, replay, expiry, two users, generation,
revocation, capacity, malformed input and denied issuer UIDs. Hosted Linux
container integration additionally creates private numeric UID fixtures, runs
real Broker/supervisor/BBS processes and Telnet games, checks independent player
identity, private dropfiles, leases, detached children and no legacy fallback.
It never runs on private CI hosts and does not provision production accounts.

Still required: native FreeBSD isolation backend, Solaris strategy, SELinux policy
for new services, production service packaging, actual AREA-PC88 integration,
persistent-game-server restart fixture, broader timeout/kill/resize stress and
security review. Do not mark LN-38 complete or deploy this candidate yet.
