# Solaris Door: nosuid execution-root experiment

Status: implementation candidate, **not a supported Door backend**. The previous
[credential experiment](DOOR_IDENTITY_FIXTURE.md) demonstrated that the basic
privilege limit alone does not retain effective/saved non-root IDs. That failure
remains valid; it is not a timing problem. This new experiment tests an additional
filesystem barrier. Runtime Solaris authenticated isolation stays disabled.

## Implementation and limits

`ci/solaris_door_jail.py` defaults to a read-only plan. Its explicit root run
requires an immutable, checksummed installation and one disposable 64MiB tmpfs.
`tests/solaris_door_jail_probe.c` executes only a fixed synthetic reporter. Before
exec, the child closes nonstandard descriptors, changes both filesystem root and
working directory, clears groups, drops to numeric slot UID/GID 61001 and applies
the basic privilege limit. No shell or arbitrary command is accepted.

The root parent reads the waiting, unreaped child's real/effective/saved IDs and
group count through `/proc/<child>/cred`. The child receives no `/proc` or contract
descriptor. It checks absent outside paths, closed inherited descriptors and
denial of another chroot. It never forks, signals other processes, writes files
or contacts a network. Each child has a five-second deadline. The still-unreaped
direct-child PID cannot be recycled to an unrelated process.

Eleven cases cover plain execution plus setuid/setgid to synthetic BBS, Broker,
consumer, other-slot identities and root. Each set-ID case first requires a
working control outside the execution root. Missing libraries, invalid reports,
failed controls or denied plain execution are errors. Set-ID execution refusal
can count only after its matching control succeeds. Namespace denial requires
ENOENT, not an ambiguous permission failure.

The root contains only the probe copies and protected system dependencies copied
from fixed Solaris 64-bit library directories (32MiB copy budget). No host lofs
mounts, device nodes, `/proc`, `/system/contract`, shells, passwd database or
writable slot directory are exposed. Live mount-table options and native statvfs
must confirm the nosuid barrier; a successful mount command alone is insufficient.

Portable tests cover refusal, incomplete evidence, tampered bundles, identity
escapes, dependency paths, failure receipts and failed-unmount preservation.
Normal Solaris CI additionally compiles the observer with warnings as errors and
tests unprivileged refusal. CI never mounts or runs this root experiment by itself.

## Separate approval required

The previous fixture approval did not include mounting. Obtain explicit approval
for this scope before installing or running the new fixture:

1. Solaris verification host 10.0.0.52 only, with existing administrator key and
   host-key verification. Use reviewed main after applicable Hosted CI succeeds.
2. New root-only `/opt/liner-notes-ci/door-jail` with the four sources below and
   manifest. Refuse replacement of an existing installation.
3. Share `/opt/liner-notes-ci/door-boundary/fixture.lock` with the installed prior
   experiment: both use the same reserved numeric identities and must not overlap.
4. One new `/var/tmp/liner-door-jail-*/root` tmpfs (`swap`, `nosuid,size=64m`).
5. Fixed synthetic tests, exact-path unmount and explicit-file cleanup; retain
   root-only plan/result receipts. No accounts, services, sudo, packages, zones,
   networking, vfstab, reboot or production changes.

Preparation on the reviewed checkout:

```sh
python3 ci/solaris_door_jail.py --plan
python3 ci/solaris_door_jail.py --prepare /private/new/jail-bundle --source-sha FULL_MAIN_SHA
```

Compare every bundle hash with that exact main source independently; accepting a
supplied SHA does not prove branch trust. Normal native compilation/refusal on
the same main SHA precedes the root test. No PR code runs on home VMs.

After approval and private transfer, in the administrator shell:

```sh
umask 077
# BUNDLE refers to the privately transferred, independently verified bundle.
test ! -e /opt/liner-notes-ci/door-jail || exit 1
test ! -L /opt/liner-notes-ci/door-jail || exit 1
mkdir -m 700 /opt/liner-notes-ci/door-jail || exit 1
for file in solaris_door_jail.py solaris_door_boundary.py solaris_door_jail_probe.c solaris_door_identity_probe.c manifest.json; do
    cp "$BUNDLE/$file" "/opt/liner-notes-ci/door-jail/$file" || exit 1
    chown root:root "/opt/liner-notes-ci/door-jail/$file" || exit 1
    chmod 600 "/opt/liner-notes-ci/door-jail/$file" || exit 1
done
/usr/bin/python3.13 -I /opt/liner-notes-ci/door-jail/solaris_door_jail.py --run-approved
```

The reused Python helper is checksum/trust checked before import. The command
flag does not itself establish user approval. Exit 0 means only the credential/
namespace matrix passed; exit 2 means a complete matrix exposed a violation.
Other nonzero exits indicate incomplete execution or cleanup failure. None
enables the backend or proves full Broker isolation.

## Evidence and recovery

Before mounting, `plan.json` is written and fsynced with the exact path/source
hashes. `result.json` retains source and dependency hashes, matrix results,
bounded command output and exit codes, errors and `cleanup_complete`. Keep the
console transcript with these root-only files. There are no secrets or real BBS
users in the fixture data. Missing receipts and incomplete cleanup are failures.

Cleanup removes only enumerated single-link regular files and empty directories,
then unmounts the exact `swap` tmpfs and confirms its disappearance. It does not
recursively delete a computed path, force unmount, or signal enumerated PIDs.
Interruption stops and reaps the owned command group; synthetic children have
independent deadlines. SIGKILL/host failure can still bypass final cleanup.

If unmount fails, preserve the private directory. Do not reuse it or force-remove
it. Use the durable plan to inspect the exact mount entry and fixture state.
After confirming the fixture stopped, no synthetic process holds it, and it is
still its own swap/tmpfs without nested mounts, unmount that exact path without
`-f` and verify absence. Remove only code-enumerated files/empty directories,
retaining evidence. Unexpected objects or mount types require investigation.
Removing the new installed bundle must not remove the prior fixture/shared lock.

## Remaining gates

Even a passing matrix leaves owned process-contract cleanup, nested-contract
creation after exec, detached/double-fork descendants, supervisor death and
unrelated-process safety unverified. PTY/ticket FD handling, real game dependency
and data provisioning, Broker revocation/restart, admission-slot reuse and an
independent safety review remain required before enabling the backend.

Local/mock checks are not native isolation evidence. Record the future root
result and exact main SHA here only after executing it.

## Primary OS references

- [mount_tmpfs(8)](https://docs.oracle.com/cd/E88353_01/html/E72487/mount-tmpfs-8.html): size and nosuid options; invalid options can be ignored, hence live verification.
- [exec(2)](https://docs.oracle.com/cd/E88353_01/html/E37841/exec-2.html): set-ID handling and preservation of the process root across exec.
- [chroot(2)](https://docs.oracle.com/cd/E88353_01/html/E37841/chroot-2.html): outside directory descriptors matter; chroot alone is not claimed as confinement.
