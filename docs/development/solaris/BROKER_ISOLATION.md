# Solaris Broker isolation: implementation gates

Date: 2026-11-20. Target: Oracle Solaris 11.4 CBE AMD64.
This is development documentation, not a supported deployment procedure.

## Current state

A subsequent [nosuid execution-root fixture](DOOR_JAIL_FIXTURE.md) now prepares
the next credential/namespace experiment. Its mount operation requires separate
approval and a reviewed-main native build. It does not enable the backend or
claim descendant/PTY/Broker acceptance.

Broker peer credentials already use `getpeerucred` / `ucred_geteuid` with proper
freeing. Privileged Door execution remains fail-closed: `door_processes.supported`
and the C isolation adapter do not enable Solaris. Native PTY support for ordinary
Doors and working libssh dependencies do not establish this isolation boundary.

The approved root matrix on main `a1dd7130045cd7ee032426476254517dee391f15`
now demonstrates that a basic privilege limit **does not confine OS identities**:
all four non-root role setuid cases and all five setgid cases changed effective
and saved identities. Only the root-setuid case was suppressed. The complete
[result and cleanup record](DOOR_IDENTITY_FIXTURE.md#completed-root-matrix-identity-confinement-failed)
is retained. This rules out enabling the current contract/basic-limit approach.

Read-only investigation used the actual VM headers `libcontract.h`,
`sys/contract/process.h`, `priv.h`, `sys/priv.h` and Solaris's own process(5),
setpflags(2), ct_ctl_abandon(3CONTRACT) manuals. Source snapshots are in local
`build/solaris-contract-headers.txt` and `build/solaris-contract-manual.txt`.

## Added foundational native test

`tests/solaris_door_contract_probe.c`, called by the Solaris-only case in
`tests/test_door_processes.py`, creates a NOORPHAN contract containing only its
own synthetic child and detached grandchild. It verifies ownership before
abandoning the contract, checks both descendants lose their pipe writers and the
leader dies with SIGKILL, and tests reducing the child's privilege limit to basic.
It never changes accounts, files owned by other identities, service definitions
or the host's policy. Parent death also abandons this non-INHERIT contract.
Deadlines and nonzero exits make failures visible. The existing native `make
check` runs this after main integration; PR/Windows cannot claim it passed.
Its explicit output says isolation is NOT verified. The backend is not enabled.

## Reserved Solaris Door slots

The Solaris slot candidate persists a BUSY marker while it holds the existing
record-lock lease. It looks for the slot UID in the real, effective and saved
UID columns of a complete process snapshot. An owner can release its slot
after stopping the owned contract and confirming no live matching process.
An incomplete snapshot fails closed. A supervisor crash leaves the marker in
place even after the record lock disappears and no matching process is visible.
Any recovery tool must establish the launch scope before it can clear such a
marker. The [direct Solaris tests](evidence/slot-lifecycle-20261123.txt) exercised
startup abort, contention, cleanup and crash retention. The candidate is
available for the Solaris launcher but is not wired into runtime activation.

## Security blockers before backend activation

1. A malicious Door may create nested/new contracts. NOORPHAN's kill scope and
   ownership/inheritance semantics must be tested; simply setting the flag is
   not proof that all descendants remain contained. Include double fork, setsid,
   parent SIGKILL and unrelated-process survival.
2. The native set-ID matrix has failed identity confinement. Limiting privileges
   to basic leaves non-root setuid and setgid credential transitions available.
   A different execution barrier must prevent those effective/saved ID changes;
   basic privileges must not be used as an equivalent of no_new_privs.
3. After the barriers are proven, implement the backend and C adapter, then test
   real distinct UIDs, supplementary groups, memory/FD access denial, PTY input,
   resize, ticket lifetime, consumer checks and durable BUSY cleanup on one SHA.

Privileged fixtures require Hosted review/main integration before execution,
a fixed disposable directory and collision-checked identities. The initial
credential fixture installation and limited update/run have explicit user
approval and are complete. That approval does not extend to new mounts, zones,
accounts, services or sudo policy. The runtime's Solaris refusal is unchanged.

The candidate [credential boundary fixture](DOOR_IDENTITY_FIXTURE.md) now
implements the executable setuid/setgid controls, real/effective/saved identity
observations, cleanup and explicit source/approval gates. Its ordinary native CI
path is non-root; its separately approved root run now supplies the failed
identity-confinement result described above.

If ordinary host process contracts/privilege sets cannot guarantee these
properties, select a separately isolated zone with a controlled nosuid execution
filesystem rather than weakening the requirements. Zone provisioning requires
its own concrete plan and approval; it has not been implemented or assumed.

Primary references:
- [Oracle process contracts](https://docs.oracle.com/cd/E86824_01/html/E54775/process-4.html)
- [Oracle privileges](https://docs.oracle.com/cd/E88353_01/html/E37853/privileges-7.html)
- [Oracle process filesystem](https://docs.oracle.com/cd/E88353_01/html/E37852/proc-5.html)

## Evidence boundaries

Windows executes portable backend tests; Solaris-only cases are skipped there.
Main `a1dd7130` passed Solaris Native CI, but its separate root identity matrix
failed confinement as recorded above. The independent review's requirement to
prove these barriers before enabling launch is unchanged. No real game,
production data or host services were changed.

## Nested-contract observation candidate

The follow-up probe also accepts `--nested`. After the same basic privilege
reduction, the synthetic Door opens a second template with no NOORPHAN flag,
forks a detached child into that new contract, and reports readiness. The
original harness abandons only its own outer contract and records whether the
nested child still holds the pipe after 250 ms. Both outcomes are **capability
observations**, not an isolation acceptance or authorization to enable launch.
An observed survivor confirms that outer-contract cleanup alone is insufficient.

Cleanup is independent of the mechanism under investigation: only the original
harness holds a watchdog pipe's write end; the nested child exits on EOF or a
five-second deadline. The harness closes that pipe, verifies all output writers
are gone, and reaps its direct child. It neither enumerates/signals unrelated
PIDs nor changes user accounts or services. Native failures retain stdout/stderr
in the unittest failure. The current backend still explicitly refuses Solaris.

The VM's exec(2) manual describes set-user-ID/set-group-ID transitions separately
from privilege-set transformations; basic limit sets are not accepted as proof
of non-root UID/GID confinement. Current headers provide no directly equivalent
no_new_privs switch established by this investigation. A reviewed, privileged
synthetic set-ID fixture or a separately isolated execution environment remains
necessary before enabling the backend. Read-only source evidence is stored in
`build/solaris-exec-privileges.txt`. The initial observation installed no privileged
fixture; the later approved root matrix is recorded separately above.
