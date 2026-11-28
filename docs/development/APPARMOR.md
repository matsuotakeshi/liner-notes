# Debian AppArmor policy design

AppArmor adds pathname and process rules to the existing service users, DAC,
systemd and BBS authorization. The optional package ships named profiles without
executable attachment rules. systemd selects each service profile explicitly.
This confines the selected service even though other programs call the same
Python interpreter. The unit asserts AppArmor availability. A missing named
profile causes the exec step to fail.

| SELinux boundary | AppArmor rule source | Inputs and limits |
| --- | --- | --- |
| BBS and embedded SSH | liner-notes | Listeners, individual config files, Core state and logs, dedicated SSH key paths |
| Web connector | liner-notes-web | Web configuration and spool, BBS protocol connection, the link management socket |
| Link connector | liner-notes-link | Link-specific state, peer certificates, node key, BBS credential and mutual TLS |
| Offline management | liner-notes-admin | Explicit aa-exec, offline DB and backup storage, fixed management wrappers |
| File transfer | liner-notes-transfer | Mandatory transition, spool and resume receipt, inherited RPC, exact helpers |
| Door Broker | liner-notes-door-broker | Broker configuration and runtime socket, no BBS data |
| Isolated launcher | liner-notes-door-launcher | Root-owned policy, scoped capabilities, slot reservations, PTY and UID process checks |
| Door game | liner-notes-door example | Named transition for a single approved game and its own writable paths |
| External Broker consumer | site-specific | Consumer is an external application. Its executable, IPC and persistent data policy require separate acceptance |

Door games need per-game transitions and exclusive store directories. The
stock profiles omit game execution transitions. The supplied rule example is
for a separate-UID isolated Door. Unmapped games fail at exec. The launcher
drop-in restricts real AF_INET/AF_INET6 socket creation independently of AppArmor
and retains Unix sockets needed by Broker and game-server IPC. AppArmor
network/address-family and Unix-socket mediation depend on the kernel
features. Per-port and per-peer restrictions remain the responsibility of
systemd, firewall, DAC and application authentication.

References: [Debian profile syntax](https://manpages.debian.org/trixie/apparmor/apparmor.d.5.en.html),
[Debian administration](https://wiki.debian.org/AppArmor/HowToUse),
[systemd service properties](https://manpages.debian.org/trixie/systemd/systemd.exec.5.en.html),
[Ubuntu kernel integration](https://documentation.ubuntu.com/security/security-features/privilege-restriction/apparmor/).

Test dependencies: Debian 13 amd64 with AppArmor enabled; parser compile-only
in a Hosted container; live Enforce and denial evidence on trusted main in
the designated Debian VM only. Debian 12 and Ubuntu compatibility remain
unverified.


## Packaging and acceptance

The existing Debian package workflow compiles the policy with
`apparmor_parser -Q -K`, installs the separate DEB, upgrades the standard
packages and the policy together to a second revision, and purges the policy.
The test checks that administrator-managed local rules and activation drop-ins
still have the same bytes. The optional DEB is a separate Actions artifact
with its base version, source SHA and SHA-256. It is outside the standard three
DEBs consumed by the production deployment adapter.

The main-only Debian AppArmor workflow defaults to a read-only inventory over
the existing Debian CI SSH identity. Its `enforce` input invokes a separately
installed fixed helper and verifies that the helper hash matches reviewed main.
The helper is an independent file-boundary probe: it uses private test
profiles and tests with synthetic files accessible to the same unprivileged UID
without a profile. It checks Enforce labels and kernel denial events. Denied probes are spaced out to avoid exhausting the kernel journal rate limiter. It does
not run the full BBS, Web, link, SSH, Door or transfer workloads.

After approval, the administrator installs the reviewed `ci/apparmor_root.py`
as root-owned mode 0755 at `/usr/local/libexec/liner-notes-apparmor-ci`.
Verify its SHA-256 against reviewed main. The proposed sudoers entry must be
validated with `visudo`. It has a fixed executable and one exact SHA argument:

```sudoers
ci-test ALL=(root) NOPASSWD: /usr/local/libexec/liner-notes-apparmor-ci ^[0-9a-f]{40}$
```

The host needs AppArmor already enabled, the parser, systemd, a kernel journal
with AppArmor events, Clang, and curl. The helper loads only random test profile
names, checks for residual uses before removal, and leaves failed fixture
inputs available for administrator diagnosis. It does not use site service
units, site data, or custom AppArmor includes. A normal CI run cannot provision
the sudo rule or helper.

Full service acceptance is a separate gate on reviewed main. The lab needs
isolated package instances and synthetic database, key, and user fixtures.
Verify login, persistence and WAL, Web writes, link TLS and signed replication,
embedded SSH, real transfer and resume, Door PTY plus Broker, and offline
backup/restore with the actual service profiles. Record kernel feature support,
package lifecycle results, and the Enforce labels. SSH, transfers and Door
remain development candidates until these workloads and refusal cases pass.
Ubuntu and Debian 12 are unverified. The site team receives only the canonical
manual after a formally approved release.
