# LN-39: built-in SSH candidate

Status: implemented candidate, not a released or production-approved feature.
Base: main `3744b889b8e60dae159591f06f16dfe52ce25936` (RTBBS/Core #131).
The stable 0.74.1 manual retains the existing OS sshd wrapper instructions.

## Architecture and security boundary

The runtime adds a second, opt-in listener backed by system libssh >= 0.10.
Telnet/RAW and SSH share the existing connection slots, Session, serialized
account/board worker, ACL, revocation epochs, quotas, notifications and Door
engine. No SSH cryptography, BBS command proxy or second copy of the DB is added.

A dedicated frontend process was considered: it isolates parser faults, but
requires a new authenticated IPC protocol for password/key results, session
revocation, flow control and PTY lifecycle. This candidate instead uses a small
nonblocking transport module in the current event loop. Password hashing and
account/key-file checks stay on the existing worker; UI never decides admission.
This choice does NOT provide process isolation from libssh vulnerabilities.

The module retains one authentication message until the worker answers. It
submits `a` for password authentication or internal `BOARD_SSH_KEY` only after
libssh reports a valid signature. That internal operation is not a text command.
Core checks repository health, active/pending/deleted/suspended membership and
the configured numeric-member/key binding. A delayed result cannot authenticate
a new connection reusing the slot, or a revoked account epoch. Credentials are
copied into the existing cleansed worker request, never into argv or environment.

Only one authenticated session channel is allowed. A PTY request must precede
shell. Shell means **BBS interaction**, never an OS shell. Exec, SFTP/subsystems,
SCP commands, direct/remote TCP forwarding, X11, agent forwarding and environment
requests are rejected. No PAM, OS password database or OS authorized_keys is used.
Remote terminal dimensions use the existing BBS normalization and Door resize
API. BBS uses its line terminal; an actual OS PTY is allocated by the existing
Door engine when a game starts. A client cannot supply a command or executable.

SSH and Telnet share the configured connection cap. Authentication (including
key probes) defaults to six attempts; handshake/authentication/channel startup
has a 60-second total deadline. Shell sessions use existing idle/account-level
limits. Input/output buffers and per-pass message work are bounded. Idle,
disconnect, shutdown and administrative disconnection use existing cleanup.
Audit stderr logs contain accepted/rejected and slot, not passwords/key material.
CONNECTIONS identifies SSH. Durable USERLOG retains the compatible RAW transport
tag for SSH sessions; it still records the BBS member, connection and duration.

**Trusted Door boundary:** installed games still execute as the service UID.
The host key is readable by that UID and the runtime holds private key material.
This feature does not claim protection against malicious same-UID Door code.
Do not install untrusted games. UID isolation and the Door authentication Broker
are separate work; no Broker or game API is implemented here.

## Build and operating systems

```
make clean  # when switching an existing checkout between SSH/no-SSH builds
make SSH_CFLAGS=-DLINER_SSH SSH_LIBS=-lssh
```

Install libssh headers through the OS's normal development package procedure
(`libssh-devel` on Rocky/Alma, `libssh-dev` on Debian/Ubuntu). Ordinary builds
without these variables retain the old dependency set and reject SSH activation.
RPM/DEB builds enable libssh and declare the dependency. FreeBSD Ports adds an
optional `SSH` build option (off by default); the existing Origin is unchanged.
Enabling it uses `security/libssh` and `/usr/local` include/library paths.

Hosted Linux CI builds and runs encrypted interoperability with libssh and
OpenSSH/Paramiko. Existing native CI still checks the common runtime with its
installed dependencies. FreeBSD encrypted-runtime and Solaris libssh builds
remain **unverified** until the trusted-main native environments have the
dependency and run this test. No VM packages are installed by this PR.
Windows SSH is unsupported. Dedicated SELinux SSH port/key labeling and denial
tests are also pending; do not call this Enforcing SSH acceptance completed.

## Candidate configuration

The listener is disabled unless `--ssh-port` is nonzero. Existing Telnet port
and arguments remain unchanged. Both board and member stores are required.

| Option | Default | Meaning |
| --- | --- | --- |
| `--ssh-port` | `0` | Disabled; otherwise 1..65535 |
| `--ssh-bind` | `127.0.0.1` | Numeric IPv4, independent of Telnet |
| `--ssh-host-key` | none | Required private host key file |
| `--ssh-member-keys` | none | Optional public-key allowlist; passwords remain enabled |
| `--ssh-attempts` | `6` | Maximum 1..20 authentication attempts |
| `--ssh-timeout` | `60` | Maximum 1..300 seconds before shell starts |

Create a new dedicated host key once in a directory controlled by the service
administrator. Do not reuse OS sshd keys, overwrite existing keys on upgrade,
or generate keys as a package-install side effect. Example in an isolated copy:

```
umask 077
ssh-keygen -t ed25519 -N '' -f /path/to/private/ssh_host_ed25519_key
./runtime --menu --telnet --ssh-port 2223 --ssh-host-key /path/to/private/ssh_host_ed25519_key 2222 notes.dat members.dat
ssh -tt -p 2223 alice@127.0.0.1
```

Here `alice` is an existing **BBS member**, and the password is the same BBS
password used via Telnet/Web. OS user `alice` is unnecessary. Registration and
password reset use existing BBS/admin flows. Verify the host fingerprint before
connecting. Password authentication cannot be disabled by enabling member keys.

Host key: regular, non-symlink file, owned by root or runtime UID, with no group
or other permission bits; it must be readable by runtime. The containing
directory must not be writable by untrusted accounts. Back it up privately and
preserve it on upgrade. Lost/rotated host keys require client trust updates.

FreeBSD rc.conf variables: `liner_notes_ssh_port`, `_ssh_bind`, `_ssh_host_key`,
`_ssh_member_keys`, `_ssh_attempts`, `_ssh_timeout`. Defaults match the table;
use the full `liner_notes` prefix for each. On systemd, insert the corresponding
options **before PORT** in `LINER_NOTES_OPTIONS`; the existing service/UID is used.
No existing sshd, service, firewall or production setting is changed automatically.

## Per-member public keys

An administrator converts a member's plain OpenSSH **public** key into a row:

```
python3 scripts/ssh-key-fingerprint.py 42 alice.pub
```

The output is `42` followed by a 64-character SHA-256 hex digest of the SSH public
key blob (not the text/comment). Add that row to the configured allowlist;
multiple rows per numeric member ID are supported. Numbers are immutable BBS
member IDs, not OS UIDs. Never store a private client key in this file.
Options such as `command=`, wildcards and OS authorized_keys syntax are rejected.

The file is reread at each signed-key authentication. Empty/missing/malformed,
symlinked, oversized (>1 MiB), group/other-writable or wrongly owned files deny
key login; passwords remain available. Edit by replacing a correctly owned
regular file atomically. Removing a key prevents new logins; to invalidate an
already authenticated session, use existing SUSPEND/disconnect controls. No
runtime reload, schema migration or member-ID reuse occurs.

## Verification and remaining gates

Small checks: authentication Core unit tests including key login/pending/suspend,
architecture tests, fingerprint input validation and service-default checks.
`tests/test_ssh_transport.py ./runtime` uses real SSH v2 and ephemeral keys to
exercise passwords, correct/wrong/cross-member keys, key removal/permissions,
Telnet coexistence, Japanese display, articles/mail, denied requests, OpenSSH,
Door return/crash/timeout/resize/disconnect, auth limits and restart of old stores.
`ci/linux.sh` runs it within existing Hosted distro jobs; PRs never reach LAN.

PR #132 initial head `2d780d98aafb0d3941d83ff35249a636cb23b0ee` failed
the SSH test on both AlmaLinux 9 and Rocky Linux 9 in
[Linux compatibility run 44467748034](https://github.com/matsuotakeshi/liner-notes/actions/runs/44467748034).
The test waited for the main prompt after sending `MENU` while still in RAW
mail mode. Mail operations retain that mode; `0` exits it. The test now exits
mail on both sender and recipient connections and verifies `PING` before
continuing to Door operations. Receive timeouts also report the missing marker
and recent test-screen output. No runtime semantics or timeout limits change.
The remaining encrypted integration cases require a successful corrected CI run;
the initial run did not reach the Door/security checks after this point.

The follow-up head `9e6bf1b7d68ab7e8bf19c0419fd293fdfdc50a85` passed
that exit but failed the recipient's mail-body assertion on both distributions
in [run 44468349733](https://github.com/matsuotakeshi/liner-notes/actions/runs/44468349733).
An arrival notification redraws the main prompt, while a mail read ends with
the distinct mail-menu prompt. The test now waits for that mail prompt and
checks the sender/recipient, subject, body and end marker. The shared test reader
has deterministic stream tests covering buffered notifications, byte-fragmented
responses, preserved trailing output and a missing response. These run through
the existing `test-ssh-sources` check. Runtime behavior is unchanged; encrypted
acceptance still requires the corrected Hosted integration run.

Head `10ae8e8b7c54f5e6fe3eba2f2a0fca4124be2ef0` passed the mail case
and the Paramiko authentication/request-denial checks on both distributions in
[run 44469613101](https://github.com/matsuotakeshi/liner-notes/actions/runs/44469613101).
It failed the OpenSSH PING check with empty stdout. That invocation used pipes;
its stderr was discarded, so the exact cause remains unconfirmed. The corrected
test starts OpenSSH with a real 80x24 local PTY and `TERM=xterm`, waits for the
initial BBS prompt, then sends PING. It records the phase, stderr, terminal tail,
exit status and whether test cleanup terminated the child. Cleanup terminates
and reaps the test client on success, failure and timeout. Host-key checking,
authentication requirements and the 12-second response timeout are unchanged.
Small POSIX harness tests cover terminal size, separate stderr, early exit and
timeout cleanup before the encrypted integration test. Windows cannot execute
those PTY checks; local syntax/source tests are not interoperability evidence.
No BBS implementation or PTY acceptance policy has changed in this correction.

Do not treat source checks or Windows stub compilation as encrypted runtime
acceptance. Record the actual PR head, CI run and OS result. Pending native SSH,
SELinux and Solaris results must stay explicit. No release or deployment is
authorized by these tests.

Primary references: [libssh server API](https://api.libssh.org/stable/group__libssh__server.html),
[libssh features](https://www.libssh.org/features/). The asynchronous message API
uses supported deprecated accessors, locally annotated; other compiler warnings
and all existing CI gates remain enforced.
