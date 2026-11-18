# Authentication and login bootstrap Core boundary

Base: `8705a904af68cc911a3b2987754d281bcc11157c` (PR #106 merge).
Grouped LN-23/24 increment: registration admission, credential verification,
application persistence and the authenticated session's initial snapshot.
This is not SSH, Door Broker, MMM implementation or completion of Core separation.
MMM remains deferred until the refactor, SSH, Broker and DB work; announce before
starting MMM.

## Responsibilities and trust

- `authentication_core.c/.h`: typed requests/results, storage-health precedence,
  registration mode, form validation before account creation, existing password
  verifier, application writes, pending/suspended refusal and login bootstrap.
- `authentication_form.c/.h`: existing `FIELD|value\n` request decoder. The Core
  receives ordered fields, not terminal commands. It validates typed callers too.
- `board.c`: trusted serialized worker entry, existing board-password grant
  context, repository health/unread callbacks and unchanged wire formatting.
- `members.c`, profile/application repositories and moderation remain the
  authoritative verifier, persistence and account-state implementations. Existing
  hashing, ID allocation, schema, suspension epochs and ACLs are unchanged.
- Session/transport still own password prompts, attempt pacing, connection limits,
  credential cleansing, delivery of login results, session registration and
  subsequent epoch checks. The Core is not a network authentication endpoint.

Calls must run synchronously on the existing account/board worker after its
policy context has been installed. Do not call this store-backed API concurrently
from a new SSH or Web thread. Results own all strings; repository pointers do not
escape. Input strings and form entries are borrowed until return, must not alias
the output, and must be cleansed by their owner. The legacy adapter also cleanses
its decoded application copy. No password, hash or form data is returned.

Only `AUTHENTICATED` yields a nonzero user and current epoch. That snapshot does
not bypass later ACL/epoch checks. Pending applications and reserved-account
errors may return the login name but have an empty authenticated session.
The result is cleared before each call, including failure after earlier success.

## Compatibility and deliberate limits

Health checks precede policy/form/credential work. Closed registration is checked
before invalid forms. The serialized form remains at most 8192 bytes with at most
320 bytes per value, even for fields accepting longer values in other editors.
Repeated fields retain order, empty values and additional `|` in values remain
valid. Unknown keys, missing line terminators and invalid profile values fail
before account creation. Login does not interpret registration forms.

Account creation and subsequent profile/note writes are still separate durable
operations. A later failed field write can leave a reserved account and earlier
fields. Output clearing does not roll them back, and callers must not blindly
retry REGISTER. This increment preserves the existing error and ID behavior;
transactional registration belongs to a separately reviewed persistence change.
Pending status is returned before suspension for a new application, as before.

Bootstrap copies existing preferences, chat identity/settings, moderation epoch,
mail notice settings, draft revisions, unread article/board totals and local-time
birthday result. The unread callback uses existing board ACL and effective article
visibility without acknowledging reads. Optional mail/menu lookup failures keep
the existing empty-notice/standard-menu behavior; they are not newly treated as
credential failures. The full unread scan is preserved, not a performance change.

## Verification

- `make test-authentication-core`: real Core/decoder with repository doubles;
  health ordering, rejection, typed/wire form boundaries, pending registration,
  partial writes, bootstrap/defaults and cleared failure results (six groups).
- `python3 tests/test_authentication_core.py ./runtime`: real sockets and password
  verifier in journal and authoritative SQLite modes; wrong password, unknown
  login, pending/approved/suspended accounts, explicit admin bootstrap, preferences,
  article persistence, restart and closed registration without ID consumption.
- Existing `check` includes both targets and existing members, rights, preferences,
  detailed-application, login-notice and moderation coverage. PR hosted CI executes
  integration tests; native machines run trusted main only.

Local validation used the Windows UCRT compiler for the isolated six-group test
and syntax checks. It does not establish native FreeBSD/Linux/Solaris or Windows
runtime support. Native and real-socket outcomes must be tied to CI's exact SHA.
