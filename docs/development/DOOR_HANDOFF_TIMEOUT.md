# Isolated Door handoff timeout

Rocky Enforcing acceptance on main `ddc71f0b303c5d763b24c826dbabc0edde69d847`
failed on two attempts after the test restarted the killed launcher and
attempted a fresh Door login. The two phase traces put 5,898 ms and 5,350 ms
between the beginning of slot acquisition and entering the process scope.
The client reported `isolation-receive` with `EAGAIN`, then the service
reached PTY handoff after the client had closed the connection.

The same main change touched the Solaris-only admission candidate and its
tests. It did not change the active Linux launch path. The two traces do
not distinguish opening or locking the slot, the UID scan, and syncing
the reservation.

The candidate gives the initial PTY reply a 10-second receive deadline.
Request transmission retains its 3-second limit. Socket timeout setup
failures abort the request. Peer credentials, Broker authority, the
persistent reservation and every post-crash acceptance check continue to
be required. Optional phase timing now distinguishes the UID scan, writing
the marker and syncing it. The confined acceptance test must still enter
the Door after restart and verify cleanup and slot reuse.

A non-privileged Rocky Linux trial passed 30 tests across the socket
diagnostic, isolation admission and worker diagnostic suites. The trial
compiled the C client and checked both timeout settings on a real UNIX
socket. It did not start the confined Door services.

- [First attempt Artifact](https://github.com/matsuotakeshi/liner-notes/actions/runs/45173055064/artifacts/14818681809)
- [Second attempt Artifact](https://github.com/matsuotakeshi/liner-notes/actions/runs/45173055064/artifacts/14820260909)
