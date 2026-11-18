# Session notification Core (LN-23/24)

## Boundary

`notification_core.h/.c` contains per-session notification state and delivery
policy. It does not depend on Session, Ui, socket APIs, storage, locales or clocks.
`NotificationAudience` is an internal snapshot supplied by the trusted session
adapter; it is not a client request or an authentication capability. Member login,
account eligibility and preference persistence remain in the existing Core APIs.
Activity Core authorizes/audits administrator notices before runtime broadcasts
already formatted public text. Committed mail results supply the recipient ID;
no subject, body or sender enters Notification Core.

`session.c` maps actual modes to delivery-block flags and keeps translation,
terminal prompt redraw, partial-input preservation and output failure handling.
Core provides an eligible mail count and a borrowed public announcement buffer.
No credential, UI selection policy, DB schema or wire command changes here.
This does not implement SSH, a new UI or reliable cross-session notification logs.

## Ownership and lifetime

The event loop owns one zero-initialized NotificationState per Session, containing
account preference revision/deny, pending mail count and the 4096-byte announcement
queue. State is not shared between connections, including connections of the same
member. Runtime delivers committed settings/arrival events to each connection;
Core checks the current member against each event and rejects older revisions.
A successful login resets the mail substate before applying its saved preference.
Logout and ordinary revocation discard pending arrivals. Closing/disposed sessions
cannot accept new events; dispose cleanses the complete Session as before.
Door/binary/transfer revocation aborts the connection via the existing cleanup path.

Callers provide valid object pointers and terminated, non-overlapping announcement
strings. Admission copies the string; peek borrows from state until append, clear
or disposal. No callback, allocation, filesystem, worker lock or global mutable
state is introduced. The previous strict queue capacity condition (including its
spare byte) and whole-message rejection on overflow are retained.

## Delivery rules

- Mail admission: active, not closing, matching nonzero recipient, notifications
  enabled and interactive mode (or binary transfer returning to interactive mode).
  Count saturates at UINT32_MAX. Pure RAW clients do not accumulate mail notices.
- Mail delivery: authenticated interactive session; no Door, binary/ZMODEM, secret
  input, pending worker, pager, editor, timer, menu/classic/mail input or delete
  confirmation, chat or scan. Eligible pending count is consumed once.
- Notification OFF discards queued arrivals. ON does not replay them. Equal
  preference revisions are accepted as before; older revisions are ignored.
- Announcement admission: active, not closing, interactive. Guests may receive
  public administrator announcements. Unknown/unauthorized senders never call this
  API directly; it does not replace Activity Core authority checks.
- Announcement delivery blocks on Door, binary/ZMODEM, secret input, pending work
  and paging. Editing/chat/timer/input/scan alone do not block it, unlike mail.
  Previously accepted shutdown notices can drain while closing.
- session_tick emits announcements before mail. It refreshes the audience after
  an output failure, so a closed session cannot consume its pending mail count.
  Core clearing follows the original output attempt semantics; this is not an ACK
  queue and a failed output does not trigger replay to another connection.

## Validation

`make test-notification-core`, included in `make check`, covers recipient and state
isolation, disabled/stale/wrong-user preference updates, logout/login reset,
saturation, binary return-to-menu admission, all 2048 blocker combinations,
announcement ordering, empty/oversize/exact-capacity messages and atomic rejection.
`tests/test_adapters.c` retains the existing real Session tests and adds each
announcement blocker, administrator-before-mail order, no replay, editor behavior
and failed-output handling. Existing socket tests test_mail_arrival.py,
test_mail_notify.py and test_stage57.py retain multi-session, bilingual, raw,
Telnet, partial-input, storage-failure, cancellation and restart coverage.

Local validation: direct C test and strict-warning syntax checks of Session and
adapter tests. Full Session linking and real socket regressions run on existing
hosted PR CI. Main-only native CI remains the OS runtime validation boundary.
