# Solaris authenticated Door supervisor candidate

The runtime sends a bounded launch request to a root-owned Unix socket.
The supervisor checks the BBS peer UID and allocates a worker with its own
Broker sequencing scope. The worker reserves the first available configured
slot for the requested Door and account level. Only a contention result
permits another slot lookup. The slot remains reserved through Broker lease
revocation and process contract shutdown. The supervisor closes admission
on SIGTERM and signals workers through a socketpair. It removes its own
socket after the workers exit. A supervisor crash can leave BUSY markers
and a stale socket for inspection.

## Opt-in staging

Use the same Solaris compiler, socket and library flags as `ci/solaris_ci.py`.
Build the regular Solaris tools, `liner-notes-solaris-door-child` and a
separate runtime with `-DLINER_TEST_SOLARIS_DOOR=1`. Stage with
`scripts/install-solaris.py --destdir <stage> --python /usr/bin/python3.13 --with-door-isolation`.
The stage includes the launcher, its protection modules, a child executable,
a sample JSON policy and the disabled manifest
`share/smf/liner-notes-door-isolation.xml`. Staging does not import a manifest.
Apply the correct root ownership and remove group/other write access from
the installed launcher, Python sources, child helper and every ancestor.

Set `bbs_uid` to the non-root BBS account, `socket_gid` to a group that
can connect to the launcher, and `broker_config` to the Broker's root-owned
configuration. Run the Broker under its distinct non-root account.
The two policies must agree on BBS UID and the set of slot UIDs.
Each Door requires an approved consumer under a separate UID. The private
root-owned `state_dir` uses mode 0700. It holds durable markers and must
be outside all game roots. Each slot gets its own root-owned `nosuid` tmpfs
and a dedicated UID/GID. The confined entry point is `/door`, with its
runtime dependencies copied inside. A trusted helper outside these roots
opens the boundary. Do not install it setuid. Configure
`--door-isolation-socket` alongside the normal `--doors` argument on the
development runtime. All games configured in this mode require approved
slots and consumers.

The launcher offers `check <policy>` for a read-only configuration check
and `ready <policy>` for authenticated probes of both local sockets.
The SMF manifest expects the policy at
`/etc/liner-notes/solaris-door-isolation.json` and a ready Broker before
starting. For approved activation in an isolated service environment, use
`svccfg validate`, then import the disabled manifest and explicitly enable
`svc:/application/liner-notes-door-isolation:default`. Monitor its log via
`svcs -L`. Use `svcadm disable -s` on that FMRI for group shutdown.
Do not clear a leftover socket or BUSY marker until you have confirmed that
the instance, worker, contract and slot UID processes have all stopped.
When that ownership cannot be established, retain the reservation.

## Isolated BBS acceptance

After review and merge, build from that exact main SHA on the Solaris test
host. Copy the verified source and binaries into a protected root-owned
tree that the fixture UIDs can traverse and read. Include a
`.ci-source-sha` marker with the full SHA. Use the existing
`/opt/liner-notes-ci/door-boundary/fixture.lock` to serialize with the
boundary fixture. From the verified tree, an authorized operator runs:

```sh
/usr/bin/python3.13 -I -B ci/solaris_door_bbs_acceptance.py \
  --source-sha <full-reviewed-main-sha> --run-approved
```

The fixture checks the dedicated numeric IDs, creates a separate test
area in `/var/db/ln-door-bbs-*` and mounts one disposable 64 MiB
`nosuid` tmpfs. The BBS listens on loopback and uses temporary data.
The suite exercises the actual menu, Broker consumption, NAWS to 100×30,
return to the BBS, disconnect revocation, slot reuse, denial when a game
prints its ready line without consuming a ticket, and refusal after the
Broker stops. It records the source SHA and case results in `receipt.json`
alongside bounded-scope process logs and removes the mount after the
fixture processes are gone. This suite starts the supervisor directly.
Actual SMF import, enable/disable and reboot are separate service checks.

The root-backed test uses trusted main only and does not run automatically
from pull requests. Hosted tests cover configuration, listener lifecycle
and PTY handoff without host service changes. Solaris Native CI compiles
the helper, stages the optional service and validates its manifest without
importing it. The stable operating instructions remain limited to the
released features.

See the [direct Solaris test record](evidence/door-supervisor-20261124.txt)
for the candidate build, worker shutdown and staged manifest checks.
