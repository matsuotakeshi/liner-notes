# Pending application review Core boundary

This LN-23/24 slice follows the profile service extraction and groups private
application review, FROMBBS/SYSOPNOTE editing, persistence access and lifecycle
regression coverage. It is not a new registration or approval policy.

## Responsibilities and trust boundary

- `member_core_application_get` authorizes the authenticated actor ID and epoch,
  requires a current global administrator and a pending account, then returns an
  owned `MemberApplication`. All 13 profile fields are deliberately private raw
  values here. Ordinary `member_core_profile_get` still filters visibility and
  rejects pending accounts; it cannot request the private review mode.
- `member_core_application_set` applies the same authority/pending checks to the
  two typed note fields. Values are limited to 320 bytes; empty values remain
  distinct from absent rows. No membership state, visibility or auth epoch changes.
- Approval, rejection and deletion remain existing Member Core operations. Review
  eligibility is checked afresh on each request, so completed applications cannot
  be reviewed or have notes changed through a previously authorized session.
- APPLICATIONSET profile fields remain aliases for administrator profile editing,
  including after approval. They do not inherit the pending-only notes restriction.
- The single board-store owner implements `application_repository_load/save` over
  the existing applications table/connection. The repository is internal storage,
  not an authorization API for transports. No retained pointers, new connection,
  schema change, migration or added multi-request transaction is introduced.
- Account registration keeps its existing validation/create/save sequence. Its
  internal note writer uses the same repository only after creating the account;
  it does not grant guests the administrator review API.
- The adapter only translates outcomes and formats the existing APPLICATION /
  END APPLICATION and APPLICATION SAVED responses. Rendering follows a successful
  complete snapshot, never partial SQLite rows.

## Failure behavior and ownership

Results own their name, all profile values and optional notes; callers must not
reuse them for ordinary public profiles. Every failed get clears the whole result.
Unknown/duplicate fields, non-text values, embedded NULs, oversized values and
failed SQLite reads are rejected rather than producing partial private responses.
Failed writes do not produce success acknowledgements. No automatic database
repair is attempted. Profile caches, pending state and storage health are checked
inside the existing synchronous single-owner execution model. This API is not
thread-safe over independently changing global stores.

The review object is bounded (one MemberProfile and two 321-byte note values),
not proportional to member count. It remains internal; no wire ABI is introduced.

## Verification and remaining boundaries

Local C11/warnings-as-errors tests pass 51 Member Core groups. Four new groups
cover full private review, owned snapshots, absent/empty notes, all actor states,
SIGOP refusal, target bounds, 320/321-byte edits, approval/rejection invalidation,
store failures and whole-output clearing. Existing profile groups still pass.

`tests/test_application_review.py` joins make check. It covers detailed automatic-ID
registration, actual socket framing, admin/guest/member access, note and profile
edits, restart persistence, approval/rejection, and injected SQLite read/write
faults in disposable fixtures. Syntax is checked locally; execution is delegated
to hosted CI and subsequent exact-main native CI, not claimed as a local pass.
No full local or native suite is duplicated and no production data is used.

The public manual stays on released 0.74.1. Its application workflow is clarified
without advertising the unreleased implementation. Japanese/English administrator
HELP remains compatible. Registration prompts, password authentication, profile
store internals and other features64 operations remain outside this extraction.
Core modularization as a whole is not complete.
