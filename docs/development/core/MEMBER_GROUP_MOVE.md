# Member bulk group migration Core boundary

Baseline: main `b5864c1d3fba93f1d0cc205aeb6c94c10264c9f2` (PR #85).
This LN-23/24 slice extracts GROUPMOVE only. BOARDGROUP/recovery and profile
management remain unchanged. Commands, wire replies, ACL rules, group count,
record format and package dependencies are unchanged.

## Authority and persistence

`member_core_group_move` accepts a trusted MemberActor, source and destination
numbers, and an owned MemberGroupMove output. Core rechecks store health,
authenticated identity, suspension, epoch and global administrator status on
every call. SIGOP delegation does not authorize global membership migration.
Both groups must be 1..100 and distinct. Failure clears the output. Inputs and
outputs must not overlap; no pointer is retained. Null output is invalid.

The existing members_group_move store operation commits the actor-attributed
record before updating all group words. It includes pending, suspended,
retired and protected accounts; their lifecycle states, levels and identities
are not changed. A repeated migration with no source members still writes one
record and invalidates credentials, matching the existing operation.

After success Core advances every named account's credential epoch, including
accounts not in the source group. No epoch is advanced after rejected or failed
persistence. The output contains the inclusive member-ID bound and the issuing
administrator's new epoch, not private member information or an authority token.
The scan stops at members_count rather than MEMBER_MAX: members_name returns
NULL above that count. Count overflow is rejected before persistence.

## Lifetime and adapter responsibilities

The existing board worker serializes member mutations and completion handling;
no asynchronous callback or UI/Session dependency is added to Core. The output
is for immediate publication by this worker, not for caching or later replay.
The adapter notifies existing vault workers for named IDs through the bound,
sets all_accounts_changed/password_changed and publishes actor_epoch. Runtime
continues to revoke all other connections, including another administrator
connection, while retaining the issuing administrator's authenticated session.
Transferred/in-game connections retain their existing abort behavior.

Epoch updates are process-local and happen after the durable member record.
This is not a new cross-process transaction or recovery protocol. The existing
startup replay, journal failure behavior and runtime notification mechanism
remain responsible for their existing guarantees.

## Verification and limits

The targeted C11 test with warnings as errors passes all 38 Member Core groups.
Three new groups exercise all 100 source numbers across the four group words,
unchanged memberships, account states, attribution, repeated empty migrations,
stale actors, guest/member/SIGOP denial, invalid bounds and failed writes with
zero output/epoch publication. Persistence/catalog are stubbed in this test.

The socket regression in test_stage61_safety.py additionally checks rejected
requests do not write, another admin connection and an unaffected member are
revoked, the issuing connection survives successive migrations, and existing
restart checks retain the migrated membership. test_stage64.py rejects a
catalog-delegated SIGOP's GROUPMOVE without changing the journal. Python syntax
is checked locally; execution belongs to existing hosted and trusted-main
native CI, not an extra full local regression run.

Reviewed canonical member-admin, board-admin, permissions, command-reference
and Japanese/English adminmembers HELP: user-visible semantics are unchanged.
Only reviewed source-lock entries are refreshed. Canonical generation, links,
search and offline parity are validated. Native CI remains pending for the
new commit; no production connection, deployment or release is performed.
