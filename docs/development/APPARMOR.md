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
