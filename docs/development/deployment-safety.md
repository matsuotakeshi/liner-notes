# Staged deployment implementation review

Candidate only; no production connection, helper/key installation or deployment
was performed for this development. Public operator instructions are canonical
`docs/manual/upgrade.md`; this file records engineering limitations/evidence.

Reviewed against code on 2026-11-09:

- `ci/deploy_release.py` reuses exact-SHA release admission and CI manifests. It
  requires actual successful native/build/restore steps, not workflow green alone.
- `scripts/deploy_policy.py` separates release signatures from short-lived
  deployment authority bound to run, attempt, SHA and all three targets.
- `scripts/deploy_transaction.py` persists stopping state before touching a
  service; failed/interrupted hosts cannot be retried by choosing another release.
- `scripts/deploy_host.py` uses closed service/package operations, private copy
  verification, root-owned configuration and old-package escrow. It does not
  install automatically or offer arbitrary root command execution.
- `scripts/deploy_restore.py` compares restored data and boots the existing BBS
  on a further disposable copy as a non-root user for login/article/mail probes.
- `ci/build-freebsd-package.sh` additionally executes an unprivileged rootdir
  resolver fixture. This uses synthetic packages with scripts disabled; it is
  deliberately not evidence of full Liner Notes package/service update safety.
- `ci/test-freebsd-package-upgrade.py` extends the same private-root technique
  to the actual package payload and an independent rc configuration, retaining
  articles/mail/config, running logical backup/restore and old-package recovery.
  Account/ownership and maintainer scripts remain disabled and unverified.
- `ci/freebsd_upgrade_root.py` is a separately installed privileged acceptance
  entrypoint. It downloads current-main source through the installed A-phase
  source verifier and same-SHA package artifacts itself, requires the Actions ZIP
  digest and package SHA-256, then runs `ci/freebsd_upgrade_jail.py` in a private
  VNET jail. It takes no source/package/command paths from the SSH user.
- `freebsd-upgrade-acceptance.yml` first verifies actual package execution on a
  GitHub-hosted runner. A documentation-only skipped package run does not contact
  the test LAN. No production host is referenced by the privileged fixture.

Known deployment blockers, not manual user acceptance tasks:

1. Full FreeBSD current-package-to-new-package upgrade, maintainer scripts,
   service checks, preserved site settings/data and old-package recovery need a
   disposable privileged fixture on the existing trusted-main CI environment.
   Until `FREEBSD-SERVICE-UPGRADE.json` attests these on the same SHA, apply is
   refused. The resolver fixture cannot satisfy that gate.
2. Actual site inventory, separate backup host, root helper/public keys,
   narrowly scoped sudo, private health accounts and Environment signing secret
   require a separately approved setup. They have not been placed on production.
3. External game daemons require their own consistent-backup integration. The
   helper refuses live dedicated-account workers, rather than stopping unrelated
   services or assuming a live tar is a consistent game backup.
4. Optional SELinux package must enter same-SHA package and Release Set manifests;
   allowlisted name alone does not authorize its installation.
5. FreeBSD's standard package does not currently ship `liner_notes_link` rc.d.
   A manually started link daemon is refused before BBS shutdown. Managed link
   update needs separately approved rc/PID/dedicated-account setup; the fixed
   service allowlist is not evidence that this operational integration exists.

Hosted fixture CI proves ordering, duplicate application, partial failure,
signature/approval/hash denial and recovery stopping; it does not impersonate
native OS service validation. Real RPM/DEB package upgrade containers use the
same offline package command builder as the host helper.

## Privileged FreeBSD acceptance prerequisites (not yet installed or executed)

The designated FreeBSD VM needs a VNET-capable FreeBSD 15/amd64 kernel, Python
3.12, pkg and the existing CI SSH key. Do not grant arbitrary sudo to takeshi.
From a root-owned, reviewed exact-main source copy, explicitly run
`sh ci/install-freebsd-upgrade-helper.sh REVIEWED_MAIN_SHA`; this also installs the reviewed
`ci/selinux_root.py` acquisition functions as a root-owned import (it does not
execute the Rocky helper). Check installed-file hashes against that main copy.
The installer independently compares its source with current main, refuses
symlinks/unknown existing helper bytes or directory modes, and leaves identical
files unchanged. It cannot be used as a blanket helper replacement mechanism.
Permit only `/usr/local/libexec/liner-notes-freebsd-upgrade-ci` through sudo; the
program itself accepts exactly one lowercase 40-hex SHA and checks current main
twice. Never use a writable checkout as a sudo command or module source.

Prepare `/var/db/liner-notes-ci/freebsd-template` as a root-owned, mode-0700
directory, using a verified FreeBSD base distribution and a disposable jail,
following the [FreeBSD jail handbook](https://docs.freebsd.org/en/books/handbook/jails/).
The reviewed one-time setup command is
`/usr/local/bin/python3.12 -I ci/prepare_freebsd_upgrade_template.py REVIEWED_MAIN_SHA`.
Run it only from a root-owned copy after explicit setup approval. It independently
downloads main and compares all three privileged script bytes before acting;
it pins the FreeBSD 15.1 base and previous v0.74.1 package hashes. Dependencies
are installed only inside a new bootstrap jail, using the test VM's existing
IPv4 address without adding an alias. The host package inventory is compared
before/after; no host service is started or stopped. Existing templates are never
overwritten. Initial network-enabled dependency setup is separate from the
offline VNET acceptance clone.
This is a test fixture, not a copy of the live OS or BBS. Its `root/` subtree must
contain a bootable FreeBSD userland, Python 3.12, pkg, the candidate's dependencies
(SQLite, lrzsz, py312-aiohttp and py312-cryptography), and the previous official
`comms/liner-notes` package installed normally so accounts/permissions exist.
Include WebUI/link options. No SSH keys, host configuration, mounted filesystems,
real users or BBS/game records belong in the template; `/var/db/liner-notes` must
be empty. Do not auto-delete a populated directory to satisfy this condition.
Place the verified previous package at `old.pkg`, outside `root/`, and a
root-only `template.json` with exactly `old_version`, `old_package_sha256` and
`old_release_source_sha`, recording the official release's version/hash/SHA.
Verify its release asset hash before creating the template. Preserve the setup
evidence outside the template; the CI helper only copies an already approved
template and never installs its dependencies on the host.

The jail has its own VNET loopback and no interface to the LAN. All runtime,
package scripts, account creation/ownership checks and restored application
checks execute inside it. Root-private failed fixtures/logs remain under
`/var/db/liner-notes-ci/freebsd-upgrade` for diagnosis; remove them only after
confirming their jail and devfs mount are gone. Only the secret-free JSON receipt
is returned to CI. Initial template/bootstrap and acceptance have not been run;
their absence deliberately prevents production admission.

The failure path returns only allowlisted phase names, exception categories and
a bounded exit code to CI stderr. It never includes exception messages, command
arguments, stdout/stderr or private paths. Jail phases distinguish old-service,
backup/restore, package-update, new-service, link and old-recovery checks. Full
logs remain root-private; the safe summary does not grant the CI account access
to them or convert failed acceptance into a success receipt.

For an explicitly selected SELinux component, `deploy_manifest.py --with-selinux`
requires the verified `rocky10-selinux` component. The controller obtains the
policy manifest from `liner-notes-selinux-policy` in the exact same-SHA SELinux
workflow run that passed Enforcing acceptance. Its independent package version,
size/hash/name and Release Set `components` record must all agree. The normal
Rocky core/web/link manifest remains a separate, mandatory three-package set.

Before production helper provisioning, reconcile the root-owned backup profile
with the actual runtime Door configuration and wrapper-specific save paths.
Every registered Door requires `backup_features.doors=true`, all game working
and data directories in the `doors` roots, and a Door startup/return probe. The
helper trusts this administrator-maintained declaration; it does not infer
external save paths from arbitrary wrapper scripts. An empty roots list or
false declaration is not valid merely because a game has not been played yet.

FreeBSD package generation and native suites share the existing CI-account
`~/ci/.liner-notes-native.lock`. The package wrapper reuses `host_lock`,
`run_commands` and `supervise`: a maximum 20-minute lock wait remains inside the
existing 3,300-second worker budget, with runner heartbeats, EOF/lease expiry and
signal-driven process-group cleanup. Different OS hosts remain parallel.
Approved initial root setup may acquire this existing lock through a read-only,
no-follow fd after checking owner, regular-file type, link count and inode; it
does not execute or write any CI-account-owned file. Drain older package runs
which predate the wrapper before that one-time setup.
