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
denial of chroot to a distinct, existing `/lib` directory (device/inode checked).
A no-op `chroot("/")` is not a privilege-denial control. Missing/identical targets
and errors other than EPERM fail the probe; successful root changes remain a
failed confinement observation. It never forks, signals other processes, writes files
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

The loader destination is read from the observer's ELF64 AMD64 `PT_INTERP`,
preserving its exact pathname inside the root. Do not replace it with a host
alias of the same inode. Invalid/truncated headers, missing or duplicate
interpreter segments, and paths outside the fixed loader directories are refused
before mounting. `ldd` supplies dependencies, not a substitute interpreter path.
The receipt includes the interpreter and observer digest. Child setup/exec
diagnostics use stderr separately from the observation protocol; failed launches
retain the child's exit/signal and the successful baseline control.

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

The separate synthetic holder-death check below combines the execution root
with the Solaris contract primitive. Further coverage is needed for double-fork
descendants inside this root, additional-contract creation, PTY/ticket FDs,
game dependencies, Broker revocation/restart, admission-slot reuse and
independent safety review.

Local/mock checks are not native isolation evidence. Record the future root
result and exact main SHA here only after executing it.

## First approved execution and loader correction

Source: main `4015507bffa2fdbb6617bf2f2b98a06d0d3561dc` (PR #159).
All ten Hosted PR workflows passed, and
[Solaris Native CI 44739519597](https://github.com/matsuotakeshi/liner-notes/actions/runs/44739519597)
passed on that main SHA before the separately approved root experiment.

The root fixture was installed at `/opt/liner-notes-ci/door-jail`, with its
checksummed bundle and existing shared lock. The temporary 64MiB nosuid mount
succeeded. The first `gid-bbs` baseline worked; the jailed observer then returned
78 without a credential report. This is **incomplete isolation testing**, not a
passing confinement result or an observed credential escape in the new design.

The exact source was subsequently rebuilt as the CI user in the private directory
`/export/home/ci-test/ci/liner-door-loader-gj2e9bb3`. Read-only `elfdump -i`
reported `/usr/lib/amd64/ld.so.1`; `ldd` reported `/lib/64/libc.so.1`. The root
receipt shows only `/lib/amd64/ld.so.1` had been copied. Thus the required loader
pathname was absent. The original exit 78 did not expose the child's errno, so
do not claim this was the only possible startup fault until the corrected test
actually runs. The new ELF parser and separate diagnostic channel address both
the confirmed layout defect and the missing evidence.

The root receipt is `/var/tmp/liner-door-jail-hi36t1u8/result.json`. Cleanup
completed; a separate post-run enumeration found only `plan.json` and
`result.json`, and no mount at or under that trial's root. No synthetic binaries
or mounts remain. The approved helper installation remains for reviewed updates.

Persistent repository evidence:
[root result](evidence/jail-4015507.txt) and
[non-root loader inspection](evidence/jail-loader-4015507.txt).
Local full transcript and bundle: `build/solaris-door-jail/approved-4015507bffa2/`.
The correction must pass Hosted CI, merge, and compile on the same main SHA
before repeating the authorized root experiment. Preserve the installed bundle
and manifest in a private backup before a hash-checked update; do not overwrite
the original failure receipts or expand the mount/identity scope.

## Non-root CI failure: no-op root change

On main `2a5525136d395f827801a58f850016a2acf3df02`,
[Solaris Native CI 44754533467](https://github.com/matsuotakeshi/liner-notes/actions/runs/44754533467)
compiled the observer and reported the correct `/usr/lib/amd64/ld.so.1` path,
then failed the non-root reporter assertion: flags `(0, 0, 1, 0)` indicated the
expected host namespace, closed extra descriptors, but no chroot refusal.
The expected tuple was `(0, 0, 1, 1)`. Artifact `14640350640`
(`native-ci-solaris-results`) preserves the failure on this exact source SHA;
the other ten main workflows succeeded. This is not a passed native acceptance.

The exact main reporter was rebuilt as the existing CI account in
`/export/home/ci-test/ci/liner-door-loader-a6bpdkah`, under the shared CI lock.
Both traced and untraced runs reproduced the same flags. `truss -t chroot`
reported `chroot("/") = 0`. `ppriv -v` showed no `proc_chroot` in the effective,
permitted or inheritable sets (the limit set still contained it). A separate
disposable CI-user process confirmed that `/lib` differs from `/` and that
`chroot("/lib")` fails with EPERM. Neither diagnosis used root, mounted anything,
changed accounts/services, or ran unreviewed PR code on the VM.

The correction changes the probe to an actual inward root change, after checking
that `/lib` exists and has a different device/inode pair. The native expected
denial remains **true**; the JSON acceptance flags, privilege restrictions and
mount checks are unchanged. If unexpectedly allowed, only the disposable
reporter's root moves inward; it subsequently writes to its existing pipes and
exits. Missing directories or unrelated errors cannot stand in for denial.

[Failure and reproduction evidence](evidence/jail-chroot-2a552513.txt) records
commands, source hashes, results and relevant failure lines. Full downloaded
Artifact and operator scripts are retained locally under
`build/solaris-door-jail/failure-2a552513/`. The installed root fixture is still
the previous `4015507b` bundle; no update or repeat mount test has occurred.
At that point the corrected native and root tests were pending CI/review/main
integration. The subsequent result is recorded below.

## Corrected loader/native test and unresolved jailed startup

PR #161 merged as `3d644d301448cdbb9c6faae45bf77d871757ef7d`.
[Solaris Native CI 44766390991](https://github.com/matsuotakeshi/liner-notes/actions/runs/44766390991)
passed on that exact SHA. This validates the non-root reporter and the genuine
chroot-denial check, not the root fixture or an authenticated Door backend.

The approved root helper update backed up the original bundle under
`/opt/liner-notes-ci/door-jail/backup-4015507`, verified the replacement hashes,
and repeated the bounded 64MiB nosuid experiment. The correct ELF interpreter
was copied and the first `gid-bbs` control succeeded. The jailed observer still
exited 78 with `signal=9 marker=0`; no payload observation was received.
The observer itself sends SIGKILL when protocol processing fails, so signal 9
alone is **not** evidence of an OOM kill, an external kill, or an isolation
escape. The exact blocking/failure stage remains unknown.

[Persistent receipt and evidence](evidence/jail-3d644d30.txt) includes hashes,
the same-SHA CI link, invocation and result. Full local output is retained under
`build/solaris-door-jail/approved-3d644d301448/`. The remote receipt is
`/var/tmp/liner-door-jail-2uouf9ep/result.json`. Cleanup completed; post-run
enumeration found only `plan.json` and `result.json` and no remaining mount.

The next diagnostic revision emits fixed stage markers before descriptor
closure, limits, chroot, identity/privilege changes, exec and payload checks.
It uses the existing nonblocking stderr pipe, single best-effort writes and
preserves errno; it neither adds inherited FDs nor opens files in the payload.
The parent records poll status/revents, marker read status and its own kill
attempt separately. Markers indicate **entry**, not successful completion of
the named operation; missing markers can also mean a dropped diagnostic write.
Stdout remains the binary observation/acknowledgement protocol. The five-second
child alarm, six-second initial poll, success criteria and cleanup stay intact.

Portable tests verify that real subprocess diagnostics survive JSON receipt
serialization without changing success/failure. Non-root Solaris tests verify
ordered payload markers, rejection of a wrong acknowledgement, unchanged binary
output and successful completion with a full nonblocking diagnostic pipe.
Those Solaris-specific tests require same-SHA native CI after merge; local
portable tests cannot validate the Solaris C code or root experiment.

Resume after Hosted CI/review, merge and successful non-root Solaris CI on the
new main SHA. Before the next authorized root run, back up and hash-check the
currently installed **3d644d30** bundle (do not reuse the old 4015507 expected
hashes/backup destination). Preserve both previous failure receipts. Keep the
same fixed mount/identity scope and record cleanup independently of trial
success. Solaris authenticated Door execution remains disabled.

## Loader directory permissions

On main `8976d0e8a74d13804dbac704c794a4eae35f34ae`, [Solaris Native CI
44846631533](https://github.com/matsuotakeshi/liner-notes/actions/runs/44846631533)
passed. The subsequent root experiment reached the child's `exec` marker but
received EOF before any payload marker. The observer then sent SIGKILL during
cleanup. The root receipt is `/var/tmp/liner-door-jail-3a5t44ie/result.json`.
Unmount and cleanup completed, leaving only the plan and result files.

The installer runs the fixture with `umask 077`. The fixture explicitly set
modes on the mounted root and copied files but relied on `mkdir(mode=0o755)`
for the directories between them. A non-root check on the Solaris CI host
reproduced mode `0700` for every directory in the loader and libc paths. Root
could copy files into those directories, but the slot identity could not
traverse them. This establishes a fixture layout defect. The precise kernel
or loader exit at the original EOF still requires an end-to-end retest.

The fixture now sets and checks mode `0755` on the fresh library directories
inside the disposable root and saves their modes in the receipt. The installer
keeps its private mask. The observer also verifies that descriptors 0–2 are
open and will survive exec. If a child exit is already observable on failure,
the parent records its status before deciding whether to signal the child.
Timeouts, mount options, payload checks and identity acceptance are unchanged.

[Failure and mode reproduction evidence](evidence/jail-loader-mode-8976d0e8.txt)
records the source, CI, receipt hashes and restricted directory check.
Resume after Hosted CI, review, merge and successful Solaris CI on the new
main SHA. Preserve and hash-check the installed `8976d0e8` bundle before the
next scoped root trial. Verify cleanup independently of the result.

## Opt-in synthetic holder-death trial

After review, same-SHA Solaris Native CI and explicit approval for this
additional privileged trial, use the existing private bundle and lock.
Hash-check and back up the installed bundle before updating it. Execute:

```sh
/usr/bin/python3.13 -I /opt/liner-notes-ci/door-jail/solaris_door_jail.py --run-holder-death-approved
```

The normal `--run-approved` path still runs only the eleven-case matrix.
The added mode uses that same mount and runs the new step only after the
matrix passes. The host starts the fixed launcher under
`ctrun -l contract -o noorphan` in a separate session. The launcher enters
the same root, drops to the reserved slot and execs the fixed `plain` probe.
The probe checks the privilege limit, closed descriptors, absent host paths
and chroot denial before creating a single descendant in its own session.
Both holders of the output pipe have independent five-second alarms.

The test verifies process relationships and contract membership before
signalling its own `ctrun` subprocess. It requires SIGKILL status and EOF
within the independent alarm deadline, no live workload processes, and a
still-running control in another contract. The receipt records samples before
and after the signal, output and cleanup. Control and holder are scoped
subprocess handles. Process snapshots are observation only. Cleanup uses the
existing exact-path unmount and trial-file removal.

The Solaris CI account directly confirmed the prerequisite for the new
orchestration, with the holder in its own session and a separate session for
the descendant. This control used system utilities and synthetic processes
outside the execution root. See the [control evidence](evidence/contract-holder-session-20261123.txt).
Hosted tests exercise result rejection, and normal Solaris CI compiles the
probe and checks non-root refusal. The opt-in privileged trial runs separately.

## Primary OS references

- [mount_tmpfs(8)](https://docs.oracle.com/cd/E88353_01/html/E72487/mount-tmpfs-8.html): size and nosuid options; invalid options can be ignored, hence live verification.
- [exec(2)](https://docs.oracle.com/cd/E88353_01/html/E37841/exec-2.html): set-ID handling and preservation of the process root across exec.
- [chroot(2)](https://docs.oracle.com/cd/E88353_01/html/E37841/chroot-2.html): outside directory descriptors matter; chroot alone is not claimed as confinement.
- [Solaris runtime linker](https://docs.oracle.com/en/operating-systems/solaris/oracle-solaris/11.4/linkers-libraries/runtime-linker.html): the kernel uses the interpreter pathname recorded in the executable's program header.
- [Solaris umask](https://docs.oracle.com/en/operating-systems/solaris/oracle-solaris/11.4/secure-files/default-umask-value.html): mask `077` removes group and other search permissions from new directories.
- [Solaris ctrun](https://docs.oracle.com/cd/E19082-01/819-2239/6n4hsf6h3/index.html): the noorphan option ties the contract lifetime to its holder.
