# Door Broker implementation review, 2026-11-19

## 2026-11-20 quota/parser follow-up

Implementer review found that the single socket quota could deny valid lease
checks when long-lived issuer sockets filled it. Separate bounded issuer and
consumer pools now preserve both roles; excess sockets abort immediately.
Capacity-one tests cover valid consume/check, slow-consumer saturation, issuer
reservation and cleanup. Deep JSON nesting below the line-size limit is also
rejected through the generic redacted error path rather than escaping the
handler with a decoder recursion exception. No authentication, UID or token
validation is relaxed. See [platform/SELinux work](SSH_PLATFORM_ACCEPTANCE.md).
This is still not an independent security sign-off or host-wide DoS proof.

This is the implementer's review, not an independent security sign-off. The
initial findings below describe the PR #135 candidate at the time of review;
later sections record subsequent changes, not retrospective proof for that SHA.
No production activation or private-host PR execution occurred.

| Finding | Change | Evidence to require |
| --- | --- | --- |
| Killing only the parent launcher could leave active workers/leases | Shared parent-liveness pipe; graceful parent waits for worker cleanup | Root hosted/jail fixture stops parent with an active detached Door child |
| flock alone disappears on worker crash, making uncertain state reusable | Persistent BUSY reservation, fsynced before launch, cleared only after cleanup | Fixture refuses a retained marker; no automated reset |
| Checking file size before acquiring flock permits a stale empty observation | Re-read size on the locked descriptor | Source inspection; reservation rejection fixture |
| Linux-only process enumeration/signalling cannot represent FreeBSD safely | FreeBSD kernel reaper subtree and no-new-privileges, no ps-derived PID signalling | Portable API-contract tests, main-only native own-descendant check and disposable jail integration |
| Service units could kill workers before cleanup or erase their state | Examples outside active unit paths; persistent StateDirectory, parent-only TERM, no forced KILL/restart | Hosted syntax/packaging checks; real systemd lifecycle still pending |
| Synthetic game success could be confused with real integration | Pin and inspect AREA-PC88 source; explicit protocol handoff | AREA-PC88 PR and dual-SHA integration evidence still required |

The original separation remains: only the trusted root supervisor may issue;
BBS supplies Core-authenticated identity; consumers are dedicated game-server
UIDs; every Door uses a unique concurrent slot UID; secrets are carried by FD,
not argv/environment values. Disabling the isolation service cannot activate the
legacy same-UID launch path. Ordinary Door configuration without opt-in remains
unchanged.

## Acceptance boundaries

- Portable tests exercise API rejection, malformed/failed native results,
  credential memory release, service-example invariants, ticket replay,
  generations, revocation and consumer scope.
- Windows can run those tests, but cannot establish Unix PTY/UID isolation.
- Hosted Linux executes the root integration inside its existing ephemeral
  container. It additionally checks NAWS, unauthorized mutations, replay,
  persistent game data, parent stop and descendants. Check the exact PR SHA.
- Native FreeBSD reaper/peer checks are part of the existing `make check`;
  cross-UID integration runs only inside the existing admitted package-upgrade
  VNET jail on trusted main. Existing workflow admission and helper hashes stay.
- Solaris adds peer-credential handling and its native check only. Full isolated
  launch continues to fail closed. No contract/privilege backend is inferred.

Still open: independent security review; Solaris process contracts and privilege
limits; SELinux domains/labels for the new services; installed systemd/rc/SMF
lifecycle; resource/fork exhaustion; forced worker death recovery; real
AREA-PC88 integration by its development workstream. In particular, file UID
separation is not a general filesystem/network sandbox. Administrative policy
must prevent slot groups from accessing private BBS/Broker/game-server data.

## Reproduction

```sh
python3 tests/test_door_broker.py -v
python3 tests/test_door_processes.py -v
# Existing hosted Linux CI calls the privileged fixture in its container:
python3 tests/test_door_broker_linux.py ./runtime
```

The last command must not run on a production host. On FreeBSD it requires the
admitted CI jail hostname and jail flag. The normal non-root native tests only
signal their own isolated reaper subtree. A platform skip is not a success for
that platform. Exact Actions results belong to the PR/check SHA, not an earlier
candidate. No existing successful result is reassigned to this revision.

## Linux service and MAC candidate

Current grouped work adds labelled fixed entrypoints, separate Broker/supervisor/
consumer domains, compiled-policy deny checks, opt-in installation contracts and
an adapter running the existing full Door scenario through systemd on trusted
main. See [operation and recovery](DOOR_LINUX_OPERATIONS.md). Working-directory
admission now rejects unsafe ancestors, symlinks and foreign ownership. No
runtime reset or fallback was introduced.

The 128-member/eight-generation and concurrent-handler tests exercise bounded
state growth, capacity refusal, lease preservation and final cleanup. They do not
simulate host-wide exhaustion. Existing fault injection remains. Security checks
cover no Broker/BBS impersonation, one-use tickets, per-mutation expiry, UID/FD
separation, and MAC refusal; root compromise is outside this boundary.

Validation still required for this revision: Hosted policy compilation and RPM
lifecycle, same-SHA native systemd/Enforcing lifecycle, and independent security
review. The updated native helper's protected-domain list requires an explicitly
authorized installed-helper refresh, never bypassing its workflow hash check.
The native mock consumer is not AREA-PC88: real-game evidence must name both
source SHAs and cannot be inferred from a synthetic game or an earlier run.

### Avoid inherited daemon-cluster socket permissions

PR #141 at `07fb9448370329bd4c1e6d73ba88de42c9205bca` failed the compiled
policy graph check in Actions job `135644083285`, run `44548102338`. Policy
compilation and both RPM builds completed; the failure was a forbidden
`liner_notes_t` to `liner_notes_broker_t` Unix socket `connectto` rule inherited
from `allow daemon daemon:unix_stream_socket connectto`, conditional on
`daemons_enable_cluster_mode`. The check must not ignore conditional rules or
turn the host boolean off to mask the grant. This is a candidate MAC-policy
boundary defect; the separate kernel-UID Broker admission remains unchanged.

Use `init_domain` for only the three new roles, retaining the explicit
NoNewPrivileges transition. Unlike `init_daemon_domain`, this interface does not
join the generic daemon attribute. Reference definitions:
[SELinux policy init interfaces](https://github.com/fedora-selinux/selinux-policy/blob/rawhide/policy/modules/system/init.if).
The compiled graph now additionally requires absence from `daemon`, presence of
the init-to-entrypoint type transition and `nnp_transition`, and rejects Web/link
connections to Broker. Existing allow/deny checks remain unchanged. The stable
BBS/Web/link domains and all runtime authentication code are unchanged.

Local checks cannot compile the Rocky policy on Windows. Hosted policy CI must
validate the resulting graph and RPM lifecycle; exact-main Enforcing acceptance
still follows merge and the authorized protected-helper update. No VM or
production changes are needed for this correction.

### Separate interpreter execution from resource capabilities

At `4fb17f2961fac5b0e43f4f71bb22527da474c816`, run `44551466350`, job
`135656031689`, the daemon membership, init transitions, data isolation and
socket allow/deny checks passed. The later capability check found Broker's
inherited `sys_resource` permission conditional on `corecmd_bin_sys_resource`.
`corecmd_exec_bin(liner_notes_domain)` assigns `sys_resource_type` to its input,
so the three new services acquired an unintended privilege through the shared
attribute. The test remains strict about conditional rules.

Preserve `corecmd_exec_bin` on the six pre-existing domains. For the three new
services, use only bin_t directory/symlink traversal and `can_exec`, without the
sys_resource_type membership or broad base-readonly-file execution grant. The
compiled-policy check requires absence from that attribute, no sys_resource
allow (including the root supervisor), and retained interpreter execution.
The separate zero-capability check for Broker/consumer remains unchanged.
Reference: [corecommands interfaces](https://github.com/fedora-selinux/selinux-policy/blob/rawhide/policy/modules/kernel/corecommands.if).
This modifies policy, not BBS authentication or test acceptance thresholds.
The new compiled graph/RPM lifecycle and native Enforcing execution remain CI
gates; local Windows checks do not substitute for them.
