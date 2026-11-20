# Independent Broker source review — 2026-11-20

Target: PR #153, `6d58303b7c3a1a61efc3c4b3f3c1c69906e7a343`.
The user authorized a separate review agent. The reviewer inspected source and
test design without changing files, operating VMs/production or starting CI.
The implementing agent separately reproduced the finding below. This is a
bounded independent code review, not an external certification or native test.

## Unresolved P2: closed launch records can exhaust shared admission capacity

`scripts/door_broker_core.py:60` limits new session IDs by `generations` size.
Lines 65 and 68 retain the generation record after close until its deadline
(default session TTL: 3,600 seconds, default capacity: 4,096).
`src/door_isolation.inc:28-31` creates a fresh random session ID on every launch.
Repeated authorized launch/quit cycles therefore consume the global quota even
with no active Door remaining. Other members' valid launches can be denied until
records expire. This behavior predates the PR's socket-pool separation; that
separation does not repair the generation-record quota.

Minimal reproduction from the repository root (memory only, no files/services):

```python
import sys
sys.path.insert(0, 'scripts')
from door_broker_core import Broker, Denied
b = Broker('review-bbs', {2002: [1]}, capacity=4, clock=lambda: 0)
for i in range(4):
    s = b.open(1, 1, f'launch-{i}', 0)
    b.close(s)
print('active', len(b.sessions), 'tombstones', len(b.generations))
try:
    b.open(2, 1, 'other-member-launch', 0)
except Denied as error:
    print(type(error).__name__, str(error))
```

Observed by both reviewers: `active 0 tombstones 4`, then
`Denied capacity reached`. Reduced capacity exercises the same admission branch;
this is not a measured 4,096-launch end-to-end workload.

Follow-up must preserve stale-generation/replay rejection while preventing one
member from monopolizing admission history. Evaluate per-member fair admission
or stable incarnation-scoped identities with independently monotonic launch
versions. Do not simply discard close records or increase the quota. Add repeated
fresh-ID launch/quit, another-member admission, old-close/new-generation and
replay regression cases. The existing simultaneous-session/load tests do not
establish this lifecycle property. This finding remains unresolved in the
SSH audit-harness correction; Broker safety completion is not claimed.

## Defenses confirmed by source inspection

- Separate supervisor, BBS, Broker, consumer and per-Door slot identities with
  kernel peer-credential checks.
- The consumer verifies Broker UID before sending a ticket.
- Broker-enabled Door launches use isolation, without unsafe legacy fallback.
- Child descriptor allowlisting, supplementary-group removal, UID/GID drop and
  prevention of acquiring additional privileges.
- One-time consumption, Door/BBS/consumer binding, expiry and disconnect
  invalidation; old-session close is distinct from the new session object.
- Tests cover the PR's issuer/consumer socket quotas and deeply nested JSON
  rejection.

No concrete privilege escalation or cross-member impersonation was found in
this bounded review. Absence of a finding is not proof of their absence.

## Evidence limits and remaining gates

This review did not execute same-SHA native isolation, direct process-memory/FD
access-denial attacks or host-wide resource exhaustion. Native Enforcing and
FreeBSD/Linux encrypted SSH results remain separate gates. Solaris isolated
Broker launch is unsupported and fails closed. AREA-PC88 remains subject to its
separately pinned integration evidence. No production approval or release is
implied. See [platform acceptance](SSH_PLATFORM_ACCEPTANCE.md).
