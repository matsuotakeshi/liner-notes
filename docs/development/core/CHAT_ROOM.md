# Chat room, private routing and persistence boundary

Base: PR #111 merge `2830d8886a8c2127b188e01f4f1cb5ef5f93cb2b`.
This LN-23/24 increment groups chat, private talk, presence, memo and SYSOP
notifications. It adds no public room selection, UI, SSH, authentication or Broker.

## Responsibilities and ownership

- `chat_types.h` keeps the existing ChatEvent/ChatNotify layout.
- `chat_core.h/c` owns an explicit peer registry and recent-event ring, preference
  revisions, receive/mute controls, shared send limits and administrator calls.
  It has no Session, UI, socket, member store, SQLite, Durable or mutable globals.
- `chat.c` owns the legacy singleton facade and Durable log. It supplies save/enabled
  callbacks and retains the LNCHAT01 row format, lock, append and replay behavior.
- Existing session/worker adapters authenticate and check epoch, suspension and
  feature access, format notifications and reject delivery during busy input.

Zero-initialize each core; callbacks and their contexts must outlive it. Do not
reset live peers or mutate implementation fields. Each core has one event-loop
owner. Callbacks are synchronous, borrow their event and must not reenter/destroy
that core; copy any retained event. The API is not thread-safe. Different
transports can share a core; independent test instances are not a public feature.

Numeric connection/user IDs and administrator flags are trusted adapter inputs,
not proof of authentication. Future UI/transport adapters must retain the worker
checks and session lifetime rules. The refactor does not expand guest permissions.

## Preserved behavior

Existing numeric outcomes/event kinds and chat.h signatures remain unchanged.
Limits remain 512-byte UTF-8 text, 20 recent events, 500ms shared send interval,
30-second SYSOP cooldown, 32 muted names and the current connection capacity.
Public save completes before delivery/history insertion. Failed save returns -4
without delivery, preserving the original send-rate accounting. A busy private
receiver returns -2 without charging the sender's timer. Private messages, calls
and presence never invoke the public save callback or enter public replay.

Preference snapshots update same-account connections only if not older. Room
leave retains registered outside-room users; disconnect removes their target.
Durable replay accepts only validated public S records and does not notify/write.
Restored connection IDs are zero. When a public log is active, empty-room cleanup
retains S events and restart restores recent public records; otherwise it clears.
JA/EN HELP and manual incorrectly described unconditional clearing; they are now
aligned with this pre-existing implementation. UI commands and DB formats do not change.

## Verification

`make test-chat-core`, added to `check`, directly verifies independent state,
write-before-delivery and failed saves, replay bounds/rejection, privacy, stale
preference revisions, mute/deny, busy receiver retry, rate limits, presence and
SYSOP delivery/cooldown. It passed locally with GCC C11 and warnings as errors.

New `tests/test_chat_core.py` joins existing real chat/talk/preference/kick tests
in `test`/`check`. It covers restart with legacy and SQLite data, public replay,
private exclusion, disabled logging and returning to BBS. Native execution is
pending hosted CI; no unreviewed PR has been sent to a home VM.

The following local legacy API test could not link on Windows UCRT GCC because
the existing SQLite layer requires POSIX struct flock/fcntl/F_SETLK/pread and flags:

```
gcc -std=c11 -Wall -Wextra -Werror -Isrc tests/test_chat.c src/chat.c src/chat_core.c src/storage_sqlite.c -pthread -lcrypto -lsqlite3 -o test-chat.exe
```

This is not a successful legacy/native test. No Windows storage port or warning
relaxation was added. Resume through normal hosted PR CI on supported operating
systems; use existing exact-SHA native CI after review/merge. Solaris follows its
daily/manual policy. Production and game data remain untouched.
