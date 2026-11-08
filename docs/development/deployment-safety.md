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

Hosted fixture CI proves ordering, duplicate application, partial failure,
signature/approval/hash denial and recovery stopping; it does not impersonate
native OS service validation. Real RPM/DEB package upgrade containers use the
same offline package command builder as the host helper.
