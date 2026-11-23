# Solaris Broker isolation: implementation gates

Date: 2026-11-23. Target: Oracle Solaris 11.4 CBE AMD64.
This is development documentation, not a supported deployment procedure.

## Current state

The approved [nosuid execution-root fixture](DOOR_JAIL_FIXTURE.md#completed-root-matrix-on-solaris)
passed all eleven credential and filesystem namespace cases on main
`df9e1c345c1ec445d205a654d201886283a42cfc`. The backend remains gated on
descendant cleanup, PTY and Broker acceptance.

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

## Broker peer identities on Solaris

The isolated [distinct-UID socket experiment](evidence/broker-distinct-uid-20261123.txt)
used the main-branch Broker and consumer modules with real UNIX peer
credentials. The issuer, Broker, approved consumer and synthetic Door ran
under different UIDs. The approved consumer could use its ticket once.
The Door and a consumer registered for a different game were denied, and
issuer EOF revoked the live lease. The direct experiment used a temporary
listener outside chroot. Service installation and the authenticated Door
launcher integrated with PTY and Broker still require acceptance.

## Authentication transport inside the Solaris execution root

The [integrated synthetic run](evidence/isolated-auth-transport-20261123.txt)
joins the existing nosuid execution root, an actual controlling PTY, and
the Broker on distinct UIDs. The jailed program reads its one-use ticket
from FD 3 and forwards it to the authorized external consumer over one
preconnected local socket. It has no Broker socket path inside the root.
The PTY carries status and user input. The consumer denied the next check
after issuer EOF, and the Broker then accepted a fresh issuer request.

The additional socket descriptor is part of the fixed test fixture. The
BBS service still needs an explicit production transport policy, integrated
slot cleanup, and validation of the actual launch path before enablement.

## Reserved Solaris Door slots

The candidate in `solaris/door_solaris_slot.py` persists a BUSY marker while it holds the existing
record-lock lease. It looks for the slot UID in the real, effective and saved
UID columns of a complete process snapshot. An owner can release its slot
after stopping the owned contract and confirming no live matching process.
An incomplete snapshot fails closed. A supervisor crash leaves the marker in
place even after the record lock disappears and no matching process is visible.
Any recovery tool must establish the launch scope before it can clear such a
marker. The [direct Solaris tests](evidence/slot-lifecycle-20261123.txt) exercised
startup abort, contention, cleanup and crash retention. The candidate is
available for the Solaris launcher but is not wired into runtime activation.

## Launch admission candidate

`solaris/door_solaris_admission.py` gives the launch owner a checked execution
root descriptor and the slot reservation. It uses the existing root-owned
path validation, then opens the directory with no symlink following and
close-on-exec. The live mount table must identify one exact swap/tmpfs mount
with set-ID execution and device-file access disabled. Any submount blocks admission,
including a mount of the contract filesystem. The kernel nosuid flag on the
open descriptor and a final directory identity check are also required.
These checks precede the durable slot reservation.

An interrupted owner closes the descriptor and releases the record lock,
leaving the BUSY marker for cleanup. The owner must explicitly stop and
verify its process scope before clearing the reservation. The descriptor
is available for entering the checked root in a future child launcher.
The launch candidate does not open a socket or start a process yet.
Supervisor integration still requires reviewing process and descriptor
boundaries in the child. The [non-privileged regression](evidence/door-admission-20261123.txt)
and the [live mount trial](evidence/door-admission-live-ddc71f0.txt)
record the tested source bytes. The latter confirmed admission, contention,
safe startup abort, and BUSY retention on the fixture's disposable nosuid
tmpfs. The actual game launch and owned-contract cleanup remain pending.

## Child entry candidate

The supervisor's dedicated child entry in `solaris/door_solaris_child.c`
accepts the admitted root, a PTY slave, a ticket pipe and a status pipe.
It rechecks root ownership and the descriptor's nosuid flag, enters that
directory by descriptor and accepts only the root-owned `/door` program.
The child receives its own UID/GID and drops supplementary groups. The
limited basic privilege set excludes Internet socket creation and visibility
or control of other sessions. The PTY occupies standard input and output,
the ticket remains on FD 3, and FD 4 closes on exec. All higher descriptors
are closed before exec, including any host or process contract handles.

The root supervisor must own the process contract and supply the arguments
from its trusted configuration. PREPARED marks the configured boundary.
The supervisor must separately confirm the exec result and game readiness
before handing the PTY to the BBS. The helper is a candidate for integration
and is neither installed nor enabled. Its root gate and argument rejection
were verified on a [non-privileged Solaris checkout](evidence/door-child-20261123.txt).
The [reviewed-main live trial](evidence/door-child-live-772d8d7.txt)
verified child execution, dropped privileges, descriptor closure, PTY
resize and slot release after the owned contract ended. Use directional
FIFO descriptors from a private directory. Solaris anonymous pipes expose
both ends as `O_RDWR`, which the child entry correctly rejects. The trial
unlinked each FIFO before spawning the helper. The descriptor-based directory change follows
[Oracle's C security guidance](https://docs.oracle.com/cd/E37838_01/html/E61050/gnclc.html).

## Contract owner candidate

`solaris/door_solaris_lifecycle.py` joins admission, temporary directional
FIFOs, the child entry and an owned NOORPHAN contract. The caller supplies
the trusted helper path and opens an issuer connection to the Broker
after acquiring the slot. The token is written to the FIFO before launch
and its writer is closed. The four child descriptors are the exact ones
needed by the entry helper. The supervisor receives PREPARED over a
separate channel with an eight-second deadline. Only the supervisor
receives the PTY at this point. It must confirm game readiness separately
and monitor the BBS and issuer sockets before and during relay.

Leaving the scope closes the issuer first, stops and reaps its own
contract holder, and waits briefly for the dedicated UID to disappear
before clearing BUSY. A failed issuer close, stop or UID inspection
retains the reservation. A crashed supervisor can lose authoritative
ownership. Its BUSY marker still requires recovery inspection.
The [non-privileged Solaris check](evidence/door-lifecycle-20261123.txt)
used actual FIFO endpoints and checked the lifecycle with controlled
Broker and contract responses. The [reviewed-main integration trial](evidence/door-lifecycle-broker-b7083dd.txt)
used the actual contract owner, Broker process and distinct UIDs. The
coordinator confirmed the synthetic game's readiness before handing off
the PTY. Disconnect revoked the lease and freed the slot. The Unix
socket and game-ready marker were fixture policy. The regular BBS
service, actual game configuration and stale scope recovery remain open.

## Security blockers before backend activation

1. The OS-only holder-death checks below cover ordinary same-contract
   descendants. Verify those cases again with the actual launcher and isolated
   execution root. Establish a boundary against creation of additional
   contracts before relying on owner exit to contain the full process tree.
2. The basic-privilege-only matrix failed identity confinement. The additional
   nosuid execution root retained the expected identities in the synthetic
   matrix. A backend candidate must enforce this barrier for every launch and
   verify equivalent behavior with its actual program and data layout.
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

## Native PTY descriptor handoff

The C receiver for the isolation supervisor's reply is shared by the active
Linux and FreeBSD adapter and a native Solaris test. On Solaris, the test
received a real PTY master with atomic close-on-exec, changed its window
size, and read slave output. It also checked that rejected replies and
unexpected descriptors leave no received descriptors open. The
[direct test record](evidence/pty-handoff-20261123.txt) identifies the tested
source files and observed results. The Solaris service launcher still
requires the full isolation boundary before activation.

## BBS C client in the Solaris fixture

The BBS isolation client shares request encoding, peer validation and PTY
receipt across the active Linux/FreeBSD adapter and a Solaris regression.
The [direct Solaris record](evidence/door-client-transport-20261123.txt) covers
both refusal of a non-root service before sending any launch data and a
successful request from a non-root BBS identity to a temporary root service.
The latter returned the PTY of the isolated synthetic Door. The client
resized it, observed authorization and later revocation, then received the
service completion message.

The root endpoint is a fixed test coordinator. The active Solaris backend
still needs its own service launcher, recovery rules and real game transport
acceptance.

## Non-root holder-death check

On 2026-11-23, a direct Solaris check used `ctrun` with the `noorphan`
option. The test confirmed contract membership and session IDs before sending
SIGKILL to its own disposable contract holder. In three runs the workload
included a descendant that called `setsid`. In three more runs it included a
double-forked descendant whose intermediate process had exited and been reaped.
All six runs reached EOF within two milliseconds and had no live workload
processes in the subsequent targeted process check. In every run a separate
control process in another contract continued to respond and exited normally.

This measured the system primitive without root. The corresponding launcher
test inside the nosuid root and the boundary against additional contracts
remain open. See the [procedure and evidence](evidence/contract-owner-exit-20261123.txt)
and [Oracle's ctrun reference](https://docs.oracle.com/cd/E19082-01/819-2239/6n4hsf6h3/index.html).

## PTY inside a system process contract

The native regression starts a synthetic terminal worker under a Solaris
`noorphan` process contract. It uses a real controlling PTY and requires the
foreground group to receive SIGWINCH when its window changes from 80×24 to
100×30. It verifies input and output after the resize and requires PTY closure
on both normal exit and forced termination of the holder. The forced case
also checks that a child in another session ends while a control in another
contract stays alive.

The [Solaris direct test record](evidence/contract-pty-20261123.txt) covers
the initial native run of this regression. Connecting the PTY and ticket
transport to the authenticated launcher inside the execution root remains
pending.

## PTY ticket delivery and revocation

The [ticket delivery regression](evidence/broker-pty-ticket-20261123.txt)
uses a real UNIX socket consumer and Broker on the Solaris test host. A
synthetic PTY worker under `ctrun` receives a ticket only through an inherited
pipe, consumes it once and verifies that the lease is revoked after the
issuer connection closes. A private socket pair injects the issuer identity
to run the test without root. The consumer and Broker validate each other's
actual OS peer UID. Connecting this path to the authenticated launcher,
distinct service UIDs and the execution root remains pending.

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
necessary before enabling the backend. The completed root matrix covers the
synthetic set-ID check. Runtime launch and process-lifecycle work remain.
Read-only source evidence is stored in `build/solaris-exec-privileges.txt`.
