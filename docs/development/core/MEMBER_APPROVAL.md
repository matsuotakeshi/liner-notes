# Member application Core boundary

Baseline: main `11d7a0003cbd53e1bb22ae96b7fcab10ca21ced4` (PR #76).
This LN-23/24 slice moves APPLICATIONS and APPROVE from the board adapter into
Member Core. Application details/editing, rejection/deletion, levels, groups,
RIGHTS presentation and registration remain in their existing paths.

## API and ownership

The existing owner worker calls member_core_applications or member_core_approve
with a trusted authenticated MemberActor. Both recheck store health, identity,
suspension, credential epoch and global administrator status. Guests receive
LOGIN_REQUIRED, other members ADMIN_REQUIRED; no caller-supplied admin flag is
accepted. Input/output storage must not overlap. Outputs own their values and
are cleared on failure; no session or output pointer is retained.

Application listing returns at most eight active pending accounts with their
stored IDs/names, in ascending order. Start zero means one, other starts are
inclusive, and next is the first undisplayed pending ID. Gaps, deleted accounts,
empty pages and pending-before-suspended status precedence are retained. The
existing member bound is checked before scanning. Partial results are erased
on store errors rather than exposing an incomplete successful page.

Approval requires an active target. The existing members_approve store validates
pending status, records actor/time and durably publishes the change. Only after
its changed result does Core advance the target credential epoch, exactly once,
and return MemberChange for the existing runtime invalidation path. The board
adapter does not advance the approval epoch again. No epoch notification is
emitted on rejection or write failure. A repeated approval remains a state error
(ERR rights syntax), rather than a successful no-op. Approval does not clear a
separate suspension, change level/groups, or recreate a deleted account.

Suspension's protected-target rule is not added to approval: these are distinct
existing policies. No additional privilege is granted. The existing member
journal, schema and runtime socket handling are unchanged.

## Adapter compatibility and tests

The board adapter retains APPLICATIONS / APPLICATION / END NEXT and the existing
RIGHTS response after approval. Unrelated LEVEL/GROUP/UNGROUP authorization,
including delegated SIGOP checks, stays in place. UI translation and protocol
framing remain outside Core; no new public command or setting is introduced.

Local C11 UCRT GCC tests with -Wall -Wextra -Werror pass all ten Member Core
groups. New tests cover pending-only paging, gaps/deleted IDs, highest bounds,
partial read errors, direct authorization, repeated approval, actor attribution,
failed append, cleared outputs and epoch behavior. Persistence is mocked: this
is not evidence of real fsync or native runtime integration.

tests/test_rights.py adds guest/member rejection, repeated-approval journal size
preservation and an empty application list after approval. Its existing native
regression covers login after approval, restart, rights and Japanese/English UI.
Python syntax passes locally; real socket tests remain for existing hosted CI.
Both tests already participate in make check. No CI condition is weakened or
new home-VM execution introduced for PR code.

Reviewed docs/manual/member-admin.md and Japanese/English adminmembers HELP:
existing user-visible instructions remain accurate. The CHANGELOG and internal
contract record the implementation change; reviewed source-lock digests are
updated. Canonical manual generation, links, search and offline parity are
validated separately. No production access, deployment or release publication.
