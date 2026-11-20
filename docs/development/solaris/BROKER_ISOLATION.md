# Solaris Broker isolation: implementation gates

Date: 2026-11-20. Target: Oracle Solaris 11.4 CBE AMD64.
This is development documentation, not a supported deployment procedure.

## Current state

Broker peer credentials already use `getpeerucred` / `ucred_geteuid` with proper
freeing. Privileged Door execution remains fail-closed: `door_processes.supported`
and the C isolation adapter do not enable Solaris. Native PTY support for ordinary
Doors and working libssh dependencies do not establish this isolation boundary.

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

## Security blockers before backend activation

1. A malicious Door may create nested/new contracts. NOORPHAN's kill scope and
   ownership/inheritance semantics must be tested; simply setting the flag is
   not proof that all descendants remain contained. Include double fork, setsid,
   parent SIGKILL and unrelated-process survival.
2. Limiting privileges to basic prevents acquisition of privileged operations,
   but available documentation of root-setuid suppression does not prove refusal
   of all non-root setuid/setgid credential changes. Test synthetic executables
   owned by BBS, Broker, consumer and another slot, plus root. UID/GID and saved
   IDs must remain confined. Do not equate basic privileges with no_new_privs.
3. After the barriers are proven, implement the backend and C adapter, then test
   real distinct UIDs, supplementary groups, memory/FD access denial, PTY input,
   resize, ticket lifetime, consumer checks and durable BUSY cleanup on one SHA.

The next privileged fixture must be reviewed on Hosted CI and merged before
execution on the home VM, use a new fixed disposable directory and collision-
checked test accounts, and obtain separate permission for its initial helper
installation. Existing permission to install CMake or the SMF BBS test helper
is not permission to install this root fixture. No current command relaxes the
Solaris refusal or installs such a fixture.

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
Same-SHA Solaris native results remain pending. The independent reviewer agreed
that enabling the backend before these barriers is inappropriate. No real game,
production data or host services were changed.
