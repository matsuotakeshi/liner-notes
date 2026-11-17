# Member directory Core boundary

Baseline: main `f748a5d94662adf6d01e34c5ea5efc3c96f88b7b` (PR #74).
This LN-23/24 slice starts an independent member_core module for ROLE, MEMBERS
and MEMBER status queries. Account mutations, authentication, profile disclosure,
RIGHTS and lifecycle operations remain in their existing paths. At this baseline
only queries were extracted; the subsequent [suspension service](MEMBER_SUSPENSION.md)
adds the explicit durable mutation API while keeping these queries read-only.
[Application listing and approval](MEMBER_APPROVAL.md) are extracted in the next slice.
[Rights queries and level updates](MEMBER_RIGHTS.md) follow without moving delegated group management.

## Responsibilities and trust

The worker supplies MemberActor (authenticated user ID and credential epoch).
No caller-provided administrator flag is accepted. Each call checks member and
moderation store health, identity existence, suspension and credential epoch.
The API does not authenticate passwords or turn an arbitrary user ID into a
trusted session. Authentication remains the prerequisite for constructing the
actor. Guests are allowed only for querying their own ROLE.

- member_core_role returns guest/member/admin for the current actor. Failure
  returns MEMBER_ROLE_NONE, which must never be rendered as a successful guest.
- member_core_list requires current global administrator status on every page.
  It returns up to eight owned summaries and the first undisplayed existing ID.
  start=0 means 1; other cursors are inclusive. Beyond-end cursors return an
  empty page as before. Sparse IDs and the maximum member ID are retained.
- member_core_get applies the same admin gate before looking up the target.
  It returns a single owned name, ID and active/pending/suspended summary.

Pending takes precedence over suspended, matching the existing display. These
are legacy display states, not a claim that a particular account can log in.
Reserved names/retired account representation are not redesigned by this slice.
Private profile fields, passwords, hashes, groups and other credentials are not
part of any returned structure. SIGOP authority does not grant directory access.

The service is read-only and runs on the existing single store worker. It has
no UI/socket/session dependency and retains no pointers. Results own their
names; input/output buffers must not overlap. Any failed page clears all rows,
including those collected before a store or metadata error. There are no writes,
audit events, credential changes or implicit login/session mutations.

The legacy sparse-ID traversal is preserved. This extraction does not introduce
a new SQL directory index or claim to solve all large-directory performance
issues. The bounded member count prevents a corrupt count from overflowing the
scan; pages remain compatible with the existing cursor convention.

## Integration

board.c formats the same ROLE/MEMBERS/MEMBER replies and error messages. Its old
list/status admin check moves into Core, so future adapters cannot omit it.
Suspension/resume mutation checks remain in board.c. The existing legacy identity
provider for vault/chat is unchanged; broader identity unification is deferred.

Makefile compiles member_core.c into runtime and test-adapters, includes the new
header dependency, runs test-member-core under make check and removes its binary
under make clean. Packages keep the same installed paths and interfaces. No
workflow, VM, package dependency or configuration is changed.

## Validation and documentation

Local direct tests compile with UCRT GCC -std=c11 -Wall -Wextra -Werror. All four
groups pass: roles/epoch revocation; directory ordering, holes, owned data and
maximum ID; direct admin denial and continuation reauthorization; broken stores,
partial-result clearing, name/count bounds and invalid output pointers.

Existing test_moderation.py, test_suspension.py, test_list_pager.py,
test_stage64_members.py and WebUI report/HTTP tests remain in make check. They
cover real role replies, admin gates, state display, pagination, maximum IDs and
Web rendering. Full integration/build results are pending hosted PR CI; local
stubbed-store tests are not native OS or live DB evidence. Native CI stays on
trusted main after merge. No production changes or release publication.

Reviewed docs/manual/member-admin.md and Japanese/English adminmembers HELP;
their current commands and permissions remain correct without public changes.
CHANGELOG records the refactor. source-lock records the reviewed Makefile and
board adapter plus new member_core sources. The existing canonical manual
generation/link/search/offline checks remain enabled.
