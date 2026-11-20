# Solaris authenticated Door: credential boundary fixture

Development candidate, 2026-11-20. This does **not** enable authenticated Solaris
Door launch. It provides the missing executable denial/control matrix before
choosing a backend. Ordinary Solaris Door/SSH support is a separate capability.

## Why this gate precedes implementation

The existing non-root contract probe checks NOORPHAN cleanup and observes nested
contract survival; it does not constrain all possible descendants. Solaris's
exec documentation describes non-root setuid/setgid identity transitions
separately from privilege-set transformations. `basic` is not treated as Linux
`no_new_privs`. A signed ticket cannot repair a Door's ability to become a BBS,
Broker, consumer or another slot OS identity.

Primary references, cross-checked against the VM's installed headers/man pages:

- [Oracle exec(2)](https://docs.oracle.com/cd/E88353_01/html/E37841/exec-2.html)
- [Oracle privileges(7)](https://docs.oracle.com/cd/E88353_01/html/E37853/privileges-7.html)
- [Oracle process contracts](https://docs.oracle.com/cd/E86824_01/html/E54775/process-4.html)

The installed `sys/procfs.h` declares real/effective/saved user and group IDs.
The probe reads these native fields, rather than assuming UID from environment
variables or just inspecting the effective UID. The read-only inventory on
10.0.0.52 found `/var/tmp` mounted executable with setuid enabled. The fixture
nevertheless requires positive execution controls; it never trusts that old
mount inventory as current proof.

## What the implementation does

- `tests/solaris_door_identity_probe.c`: native synthetic credential reporter,
  read-only process inventory and fixed drop-to-slot/basic-limit execution.
- `ci/solaris_door_boundary.py`: default read-only plan, immutable bundle
  generation and a separately approved one-shot root fixture.
- `tests/test_solaris_door_boundary.py`: portable refusal/classification/cleanup
  tests and a **non-root** Solaris compile/report/refusal test in `make check`.
  Hosted CI never invokes the root matrix or connects to the home network.

The slot is numeric UID/GID 61001. Synthetic BBS/Broker/consumer/other-slot IDs
are 61002..61005; root is an additional execution target, not a launched service.
The fixture refuses any existing passwd/group entry or live real/effective/saved
UID/GID using those reserved numbers. It does not create accounts. Do not run
concurrent administrative identity allocation while this fixture is active.

For plain execution and ten setuid/setgid role cases it runs a baseline control
and a basic-limit execution. Effective/saved identity changes, extra groups and
unexpected privilege limits are reported as escapes. A permission-denied exec
counts as a refusal only if the same case's baseline demonstrably honored the
set-ID bits. Missing binaries, malformed output, incomplete cases, inactive
set-ID mounts and precondition failures are inconclusive errors, not successes.

All set-ID files are synthetic copies of a reporter that only reads its own
credentials and prints numeric JSON. Its privileged modes require **both real
and effective UID zero**; a setuid-root invocation by a slot cannot invoke them.
No password, ticket, game data, BBS file or live service is accessed. Each child
has an alarm, and the wrapper enforces a deadline, kills only its own process
group and waits for it. Only explicitly recorded synthetic paths are removed.
The private receipt remains mode 0600 in a root-only directory.

Exit statuses: 0 means the complete set-ID matrix remained confined; 2 means a
credential/privilege escape was observed; an exception/nonzero command is an
inconclusive fixture failure. **Even exit 0 is not full isolation acceptance.**
The report always sets `broker_isolation_verified` and `door_backend_enabled`
false. The production backend continues to refuse Solaris.

## Review and execution sequence

1. Hosted tests and review, then merge through the existing main protection.
2. Run Solaris Native CI on that exact main SHA. This compiles the reporter and
   runs only its non-root path, checking real/effective identity and refusal of
   privileged modes. A skipped native case on other OSes is not Solaris proof.
3. Prepare a bundle from a clean checkout of that same reviewed main SHA:

   ```sh
   python3.13 ci/solaris_door_boundary.py --plan
   python3.13 ci/solaris_door_boundary.py --prepare /private/new/door-boundary-bundle --source-sha FULL_REVIEWED_MAIN_SHA
   ```

   The bundle must be a new directory. Compare its two file hashes with the
   reviewed source; the manifest's SHA label alone is not source authentication.
4. Obtain explicit approval for **this fixture's first root installation/run on
   10.0.0.52 only**. Previous SSH-library/SMF permissions do not cover it. No sudo
   rule, account, service, mount or package is proposed. Normal CI remains non-root.
5. After approval only, install the verified bundle into a new fixed directory.
   These commands are a reviewable proposal, not an instruction to run before
   approval. Use `/bin/sh`, a root-controlled incoming bundle and the test VM:

   ```sh
   test "$(uname -n)" = solaris || exit 1
   test "$(id -u)" = 0 || exit 1
   test -d /opt/liner-notes-ci || exit 1
   test ! -e /opt/liner-notes-ci/door-boundary || exit 1
   test ! -L /opt/liner-notes-ci/door-boundary || exit 1
   umask 077
   mkdir -m 700 /opt/liner-notes-ci/door-boundary || exit 1
   # BUNDLE is the verified incoming bundle path, not a caller-controlled service argument.
   for file in solaris_door_boundary.py solaris_door_identity_probe.c manifest.json; do
       cp "$BUNDLE/$file" "/opt/liner-notes-ci/door-boundary/$file" || exit 1
       chown root:root "/opt/liner-notes-ci/door-boundary/$file" || exit 1
       chmod 600 "/opt/liner-notes-ci/door-boundary/$file" || exit 1
   done
   (set -C; : > /opt/liner-notes-ci/door-boundary/fixture.lock) || exit 1
   /usr/bin/python3.13 -I /opt/liner-notes-ci/door-boundary/solaris_door_boundary.py --run
   ```

The runner validates immutable root ownership, ancestors, regular files and
hashes, and refuses source execution from a developer checkout. Compiler paths
are resolved and checked, compiler environment overrides are removed, inherited
FDs are closed and the fixture holds a dedicated root-only lock. No compiler or
policy path comes from a BBS request. It never grants root execution to `ci-test`.

The printed receipt path identifies `/var/tmp/liner-door-boundary-*/result.json`.
Preserve that JSON and the exact source SHA; it contains synthetic identities,
not authentication secrets. Normal exceptions remove the synthetic files and
leave this receipt. A host crash/SIGKILL can interrupt cleanup: inspect the named
directory before removing it; never broadly delete all matching paths or reuse
test identities while any test process survives. The reporter is the only
set-ID program created. A fixture failure requires diagnosis, not a weakened test.

After evidence is archived, remove the fixed installed files and lock explicitly
and remove the now-empty directory with `rmdir`. No recursive deletion, user
deletion, service restart or mount operation is needed. Do not remove the shared
`/opt/liner-notes-ci` directory or other fixtures.

## Remaining implementation gates

Use the identity matrix and existing nested-contract observation to select the
backend. If either barrier fails, do not enable the host contract/basic-privilege
combination. A controlled `nosuid` filesystem and inaccessible contract/proc
namespace, or a dedicated zone, requires separate design, escape tests and a
concrete provisioning approval; `chroot` alone is not assumed sufficient.

After proven barriers, implement the supervisor/C adapter, UID separation,
durable BUSY recovery and Broker/PTY lifecycle acceptance. Include double fork,
setsid, nested contracts, parent SIGKILL, unrelated-process survival, memory/FD
denial, ticket revocation and legacy Door compatibility. No runtime code in this
candidate enables launch; it is the executable safety gate, not a completed port.

## Candidate verification record

Base: main `e7df23d86f8212f5ca922b6158b1a8d9f697d430`.
Local Windows checks: boundary fixture 15 passed / 1 Solaris-only skipped;
existing Door backend 9 passed / 4 OS-specific skipped. Canonical manual: 42
pages, 2,822 links, Markdown/HTML/offline parity; search logic 8 cases passed.
Local receipt: `build/solaris-door-boundary/manual-check.json`.
Solaris read-only header/mount inventory: `build/solaris-boundary-inventory.txt`.

The native C probe has not yet been compiled from this candidate on Solaris;
LAN execution remains trusted-main only. No root bundle has been installed or
executed, and no set-ID matrix result is claimed. Root-credential fixture approval
is separate from the earlier library installations and SMF fixture permission.
