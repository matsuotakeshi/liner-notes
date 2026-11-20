# SSH platform and SELinux acceptance candidate

Base main: `ed010ee3c6a7ecbb1b64e694b7839264832fb418` (PR #152).
No production deployment or release is authorized by this work.

## Native acceptance

`Embedded SSH Native Acceptance` is manual, exact-main only. It reuses the
existing accounts, SSH/host keys, remote source checksum, host lock, heartbeat,
bounded cleanup and log artifacts. `all` means FreeBSD/Rocky/Ubuntu; Solaris
requires explicit selection and does not add another automatic daily run.
It builds with libssh, runs the existing encrypted SSH/OpenSSH/Paramiko suite,
and does not repeat `make check`. Its receipt includes `suite: ssh`; the
ordinary `core` receipt cannot satisfy this gate.

The driver fails if installed libssh >= 0.10 headers/libraries or OpenSSH are
missing. It installs Python test dependencies only into the run's new private
venv, never the host Python. It does not install/update OS packages. Source
and packages for Solaris must match x86-64; the Solaris build flags retain the
existing SQLite/OpenSSL/socket/PTY settings. A missing dependency or unsuccessful
run is not an OS support result.

### One-time VM prerequisite proposal (requires separate administrator approval)

The read-only inventory found no libssh development package on FreeBSD
10.0.0.50, Rocky 10.0.0.51 or Solaris 10.0.0.52. Do not use these commands on
production. First inspect the package plan and stop if unrelated packages would
be upgraded or removed. Do not restart any BBS, runner, SSH or host OS.

```sh
# FreeBSD test VM only; libssh includes its headers.
pkg install -n libssh
# The dry-run alone does not cover pkg self-upgrade; see the observation below.
# Do not run the installation until that separate transaction is accounted for.

# Rocky test VM only; no dnf upgrade or policy/mode change.
dnf install --assumeno --setopt=install_weak_deps=False libssh-devel
# After the proposed transaction has been reviewed and approved:
dnf install --setopt=install_weak_deps=False libssh-devel

# Verify as the ordinary CI account, not root:
pkg-config --atleast-version=0.10 libssh
pkg-config --modversion libssh
```

Ubuntu lacks pkg-config; its libssh headers have not been established. All three
FreeBSD/Rocky/Ubuntu CI interpreters have venv/ensurepip. Solaris needs a separate dependency decision;
no guessed package or root installation is included here. Python venv/pip and
OpenSSH clients must already be available. Their absence is a preparation
failure, not a reason to disable SSH tests.

## SELinux candidate policy 0.3.0

- SSH remains in `liner_notes_t`; this is not the OS sshd domain.
- Dedicated host keys under `/etc/liner-notes/ssh-host-keys/` use
  `liner_notes_ssh_secret_t`. Core and the offline administrator may read them;
  no application domain may write/create/unlink them. Web, link, Door, transfer,
  Broker, supervisor and consumer have no key-read permission.
- A separately allocated port is labelled `liner_notes_ssh_port_t`. Only Core
  may bind it. Neither installation nor testing relabels port 22 or changes sshd.
- Unix ownership and mode 0600 checks still apply. The policy does not make
  a malicious legacy same-UID Door safe; opt-in slot-UID isolation remains required.
- Administrators preserve host keys during updates and back them up separately
  with configuration. The BBS database archive alone is not a host-key backup.

Policy graph/RPM tests inspect positive and negative permissions, including
conditional broad grants. The Enforcing fixture rebuilds only its admitted
private runtime copy with SSH, creates fresh synthetic host/member keys and a
private client venv, and labels only its own temporary port/files. It uses actual
password and key login, pinned host identity, UTF-8, persisted article/mail,
exec/extra-channel rejection, normal/abnormal Door return and disconnect cleanup.
A world-readable canary carrying the host-key label proves Door MAC refusal
independently of Unix permissions. Existing Broker/systemd/backup/transfer tests
are retained. The installed privileged helper itself is unchanged.

`tests/test_selinux_ssh.py` runs the same client against a Hosted runtime first.
It has no MAC and is not an Enforcing success. Real policy/domain tests still
require reviewed main and the existing Rocky fixture.

## Broker boundary correction

The previous single socket quota was shared by persistent issuers and transient
consumers. At full issuer capacity no legitimate lease could be consumed or
checked. Separate bounded pools now allow up to `capacity` issuers and
`capacity` consumers; excess sockets abort without allocating a drain deadline.
Ticket/session/generation/lease limits are unchanged. Tests use capacity one to
prove full issuance still permits valid consumption/check, excess stalled
consumers are rejected, issuers retain their reserve, and cancellation releases
all socket state. This does not claim immunity to host-wide resource exhaustion
or an unbounded hostile connection flood.

## Gates still pending

- Hosted candidate CI, then exact-main native/Enforcing runs and package evidence.
- FreeBSD/Rocky dependency preparation is recorded below; the protected helper
  was not changed. Ubuntu/Solaris prerequisites are not yet established.
- Independent source review completed with an unresolved P2 admission-history
  exhaustion finding; native attack checks remain pending. See
  [review and reproduction](DOOR_BROKER_SECURITY_REVIEW_20261120.md).
- Solaris Broker process-contract/privilege backend: still unsupported and
  fail-closed. Peer-credential tests alone do not establish isolated Door execution.
- AREA-PC88 results remain those of its previous pinned two-repository test;
  this change neither modifies nor retests the actual game.

## VM preparation observation (2026-11-20)

The user authorized dependency addition on FreeBSD/Rocky only. FreeBSD's
`pkg install -n libssh` plan reported one new libssh 0.12.2 and no upgrade.
The later install unexpectedly self-upgraded pkg 2.8.4 -> 2.9.2 before adding
libssh 0.12.2. This was outside the intended transaction and was reported to
the user immediately; further package operations stopped, no downgrade was
attempted, and no service/OS restart command was issued. The user subsequently authorized retaining pkg 2.9.2. No rollback was performed.
Future installation plans must account for pkg self-upgrade separately rather
than treating the dry-run as a complete transaction. Do not describe this as a
libssh-only mutation.

Rocky initially rejected the first root key/password routes. The user identified
an existing administration key, which authenticated successfully. The dry-run
required 11 new packages and 7 replacements; excluding all replacements failed.
The user explicitly approved those dependency updates. The executed transaction
asserted both name sets before downloading/installing. libssh-devel/libssh are
now 0.12.2-2.el10; `getenforce` remains Enforcing. No service/OS restart command
was issued. Local preparation log: `build/rocky-ssh-dependency-preparation.log`.
The replaced packages were krb5-libs, libselinux, libselinux-utils, libsepol,
libssh, libssh-config and python3-libselinux. No other installed package was
selected for replacement/removal. Normal tests still use CI accounts, not root.


## Local candidate checks

Windows: Broker 24 passed; native receipt/dispatch tests 5 passed; existing native
orchestration 14 passed and 5 POSIX-only skipped; SELinux label tests 5 passed;
root-helper tests 6 passed and 1 POSIX-only skipped; service fixture tests 49 passed.
These are local unit results, not native encrypted/MAC proof. Syntax/diff checks
passed. Canonical manual: 42 pages, 2,822 links, Markdown/HTML parity, no errors.
Hosted integration and compiled-policy checks are deferred to this candidate PR.

## Hosted audit-harness correction (2026-11-20)

PR #153 source `6d58303b7c3a1a61efc3c4b3f3c1c69906e7a343`,
[Rocky SELinux policy run 44687080871](https://github.com/matsuotakeshi/liner-notes/actions/runs/44687080871),
failed at `Verify fresh-instance bootstrap against the real runtime` during
`Server.close()`. The encrypted client's password/key, host pinning, article/mail,
UTF-8 and exec-denial assertions passed, but the shared harness expected empty
stderr while Core emitted its normal one rejected and three accepted SSH audit
records. The compiled-policy step did not run; this is not a native AVC result.

The Hosted fixture now explicitly validates those four complete records in order,
allowing scheduling-dependent connection slot reuse. Missing/extra records,
malformed lines and appended secrets are rejected without echoing untrusted
contents. The default shared harness still requires exact expected stderr, its
shutdown deadline/kill diagnostics are unchanged, and the SSH fixture also
requires normal exit status. Core logging and policy permissions are unchanged.

Focused Windows tests: `python tests/test_selinux_ssh_audit.py` (4 passed) and
`python tests/test_server_diagnostics.py` (16 passed). The same tests precede the
real encrypted fixture in Hosted CI. These local unit checks do not establish
native encrypted or SELinux Enforcing acceptance; the corrected candidate needs
its own CI result. Canonical public commands/settings are unchanged by this
harness correction; the candidate status remains separate from stable 0.74.1.
