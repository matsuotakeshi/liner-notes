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

## Dependency preparation and follow-up (2026-11-20)

PR #153 head `c59f4de557ac4d026bbeb0f70a93f9c9ef2e8dbe` passed all 12
reported Hosted workflows, including the corrected SSH fixture and compiled
SELinux policy; merge commit is `3b6003c25d212b42d0ff17d9c14f9953c3889f72`.
Those PR results are not the merge commit's native results. Embedded SSH Native
Acceptance was dispatched once on main for FreeBSD/Rocky/Ubuntu after preparation;
its results must be checked separately without continuous polling.

With explicit per-host permission, Ubuntu added 12 packages (libssh-dev,
pkg-config and dependencies), with zero existing-package updates/removals.
libssh is 0.12.2. Package/service restarts were not requested; needrestart was
suspended. Log: `build/ubuntu-ssh-dependency-preparation.log`.

Solaris's installed publisher exposes libssh2, which is not a substitute for
libssh's server API. Its approved root transaction added only CMake 3.24.0:
no existing-package update, boot environment creation or OS/service restart.
Logs: `build/solaris-cmake-plan.log`, `build/solaris-cmake-install.log`.

As the existing CI account, official libssh 0.12.2 was built successfully with
GCC/`-m64` into `~/liner-notes-deps/libssh-0.12.2/local`. The installed shared
library is ELF AMD64 64-bit; pkg-config reports 0.12.2. It is not installed into
the OS library path. Log: `build/solaris-libssh-build.log`.
Reproduction: `ci/prepare-solaris-libssh.sh` (manual, non-root, refuses reuse).
Archive SHA-256: `49560f677d96e3706a904ac2de1116e25f3680937d51e5c92198fcba4a1c1e9f`,
checked against [FreeBSD Ports distinfo](https://github.com/freebsd/freebsd-ports/blob/main/security/libssh/distinfo)
and fetched over HTTPS from the official libssh distribution site. The CI
candidate discovers only this fixed private prefix on Solaris and embeds library
runpaths in its disposable executable. No shell profile/loader/sshd change occurs.
Encrypted Solaris BBS acceptance requires merging that CI adaptation and then
explicitly dispatching `Embedded SSH Native Acceptance` with target `solaris`.

The scoped Broker correction is documented separately; Solaris isolated launch
remains disabled pending the denial tests described in
[Broker isolation](solaris/BROKER_ISOLATION.md). Building libssh does not satisfy
that security gate.

Solaris Paramiko preparation subsequently added the five official IPS packages
`bcrypt-311`, `bcrypt-313`, `paramiko`, `paramiko-311` and `paramiko-313`, after
explicit permission for required dependency additions/updates. The actual plan
and transaction made no existing-package updates/removals. Read-only imports as
ci-test verified Paramiko 4.0.0, cryptography 46.0.3 and bcrypt 5.0.0 on Python
3.13. Logs: `build/solaris-paramiko-plan.log`,
`build/solaris-paramiko-install.log`, `build/solaris-paramiko-verify.log`.
Solaris native SSH uses a fresh venv with system-site-packages to reuse these
Solaris builds; pip additions remain in that venv and the existing requirements
bounds still apply. This dependency import is not encrypted BBS acceptance.

At the final status snapshot, main `3b6003c25d212b42d0ff17d9c14f9953c3889f72`
passed all 11 completed push/derived workflows, including
[Multi-OS Native CI](https://github.com/matsuotakeshi/liner-notes/actions/runs/44694428167),
[FreeBSD isolated upgrade](https://github.com/matsuotakeshi/liner-notes/actions/runs/44694669889)
and [Release Set](https://github.com/matsuotakeshi/liner-notes/actions/runs/44694669891).
The separately dispatched
[Embedded SSH Native Acceptance](https://github.com/matsuotakeshi/liner-notes/actions/runs/44695623639)
was still running, not passed. These results concern the merge of PR #153,
not the subsequent lifecycle/Solaris candidate.

## PR #154 integration and FreeBSD test dependency correction

PR #154 head `a89e798dc5c4a26ecee591f302075e51d7abfa39` passed its applicable
Hosted checks and was merged as `75b85c307e540116e4e5666b9557732abc098458`.
Its policy `native` job is intentionally main-only and skipped on PR; this is
not evidence of a native run. Solaris encrypted SSH and Solaris Native CI were
dispatched on that exact main, pending separate results.

The earlier encrypted SSH run
[44695623639](https://github.com/matsuotakeshi/liner-notes/actions/runs/44695623639)
finished: Rocky and Ubuntu passed on `3b6003c25d212b42d0ff17d9c14f9953c3889f72`;
FreeBSD built the SSH runtime but failed before connection tests while pip tried
to build cryptography 50.0.2's maturin dependency without Rust. Artifact ID:
`14610749106`. Failure excerpt: `build/ssh-native-44695623639/freebsd.log`.
This is a test dependency failure, not evidence of an SSH runtime regression.

The follow-up uses `--system-site-packages` for a fresh FreeBSD test venv, as for
Solaris, to reuse explicitly provisioned OS Python packages. Pip still runs only
inside the private venv, with the unchanged `paramiko>=3.5,<5` requirement. Linux
keeps its fully private venv. The test covers all four OS branches. Ordinary CI
never installs OS packages or silently skips encrypted acceptance.

A read-only FreeBSD plan identified five new packages: py312-paramiko,
py312-PyNaCl, py312-bcrypt, py312-pyasn1 and py312-wheel. The user authorized only
those five additions. Preparation must acquire the existing native host lock,
refuse upgrades/removals, and compare complete package inventories afterwards.
A lock-busy first attempt performed no package changes. The preparation result
is separate from the eventual same-SHA encrypted native acceptance.


The previously pending Solaris runs on `75b85c3` completed with failures; the
Rocky Enforcing run also failed. The separate causes, direct-server evidence,
FreeBSD preparation outcome and follow-up gates are recorded in
[platform failure investigation](PLATFORM_FAILURES_20261120.md). Do not treat
previous Hosted successes or dependency imports as these native acceptances.
