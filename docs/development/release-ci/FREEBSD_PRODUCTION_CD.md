# FreeBSD production CD: 2026-11-27 implementation record

Base: `e939a9d280107ea939e861bbd39e468fb742100d` on
`matsuotakeshi/liner-notes`. Work is isolated from the concurrent KTBBS PR #214.
No production SSH, service operation, package install, helper/key provisioning,
environment edit, release publication or CI-machine change was performed.
Operator instructions live in [the canonical upgrade chapter](../../manual/upgrade.md).

## Evidence reviewed and current boundaries

- The referenced “本番LinerNotes CD化” conversation contains the request for CD,
  with no earlier technical decision. Recent “Liner Notes BBS 開発室” turns identify
  the audit milestone, successful native/FreeBSD jail runs and concurrent KTBBS work.
- Current repository policy and the existing `production-staged-update.yml`,
  `deploy_release.py`, signed policy, transaction, host/restore adapters and
  installer already implement approval-bound three-host delivery. This change
  extends that mechanism rather than introducing another deployment engine.
- GitHub main was independently checked at the base SHA. Native CI
  [45936688552](https://github.com/matsuotakeshi/liner-notes/actions/runs/45936688552),
  Release Set [45940447969](https://github.com/matsuotakeshi/liner-notes/actions/runs/45940447969)
  and FreeBSD isolated upgrade
  [45940447946](https://github.com/matsuotakeshi/liner-notes/actions/runs/45940447946)
  report success on that SHA. This does **not** attest this new branch, nor prove
  an arbitrary installed production version is eligible for that upgrade.
- The current [v0.75 release](https://github.com/matsuotakeshi/liner-notes/releases/tag/v0.75)
  (ID 458991587) includes pkg/RPM/DEB, source, Ports, manual, RELEASE-SET.json and
  SHA256SUMS, but no DEPLOYMENT-MANIFEST.json. It is therefore not deployable with
  this controller yet. `audit-2026-11-27` is a milestone tag, not a stable `v` release.
- Production Environment reviewer/branch restrictions, secret availability,
  actual installed package/schema, root helper/profile, backup receiver and
  current reachability were not verified. The connector does not expose secret
  or Environment administration reads. Missing evidence remains a blocker;
  settings are never inferred from YAML or old successful runs.

## Connection and package paths

| Role | Configured path | Evidence / limitation |
| --- | --- | --- |
| Native CI coordinator | Existing Ubuntu runner 10.0.0.54 | CI_CD.md and multi-os-ci.yml; latest native run succeeded |
| FreeBSD test/package host | coordinator → takeshi@10.0.0.50 | ci/native_ci.py; dedicated `/home/ci/.ssh/id_ed25519_freebsd_ci`; shared native/package lock |
| Other test hosts | ci-test@10.0.0.51 / .59 / .52 | Rocky / Debian / Solaris; their own CI keys; unchanged |
| CD controller | GitHub-hosted Ubuntu → ln-deploy@srv.ysni.org | Existing direct SSH/SCP transport, pinned production_known_hosts, no ProxyJump or CI-parent dependency |
| Privileged operation | ln-deploy → one root-owned helper with no arguments | Narrow sudo entry, root-owned profile, separate release/approval public keys; installation unverified |
| Backup transport | Production helper → dedicated off-host receiver | Host key plus machine identity pin; actual destination still required |

No new self-hosted runner is required for the configured direct route. If the
production firewall disallows GitHub-hosted egress, establish an approved network
route before enabling apply. Do not install a general Actions runner on the
shared production host or place production credentials on the native CI parent.

The FreeBSD CI builds and tests the existing `comms/liner-notes` pkg and publishes
`liner-notes-freebsd15-packages` plus its source-SHA/version/hash manifest. The
existing release-set verifier binds all platform candidates. The signer consumes
that verified candidate tree; it is not a substitute for CI. CD downloads the
release's already-built pkg, verifies size/hash, and compares it to the exact-SHA
CI manifest and release receipt. No package rebuild, native dispatch or global
pkg update was added. Normal PR/main validation remains governed by existing CI;
only lightweight scope regression tests were added to its existing safety job.

## Chosen behavior

- Manual `workflow_dispatch` on trusted main only. UI defaults to `scope=freebsd`
  and `mode=plan`; legacy CLI default and `scope=all` preserve three-host behavior.
  Neither main push nor tag creation performs an update.
- Published stable `vVERSION` release plus a valid signed deployment manifest,
  release ancestry, exact-SHA CI, package/receipt checks and full FreeBSD upgrade
  acceptance remain mandatory. FreeBSD-only selection does not weaken the
  established complete release-set or cross-platform CI admission policy.
- Plan never requires or starts production SSH. It explicitly records connectivity
  as unverified. Apply requires the latest exact-controller-SHA connectivity run
  with the FreeBSD-specific executed step, within one hour. Legacy all-host
  preflight additionally checks the all-host step so a single-host smoke cannot
  be mistaken for three-host evidence.
- Every apply, including the first, requires owner review in production-release
  with an exact tag/SHA/host/run/attempt comment. Only all three ordered targets or
  FreeBSD alone are allowed in a signed authorization. Root verification rejects
  using a FreeBSD authorization on Rocky/Debian. Old helpers reject the new scope
  until an explicitly approved reviewed helper update is installed.
- Only the selected scope's packages are downloaded/transferred; FreeBSD-only
  never invokes Rocky/Debian transport. Final evidence is rechecked before a
  short-lived authorization is minted. Current controller safety CI is also
  checked separately when deploying an older release.
- Existing workflow concurrency and machine-wide host lock/persistent state are
  retained. Interrupted/unhealthy transactions cannot be bypassed with another
  release or blind retry. SSH uncertainty requires inspection of private state.

## Data and recovery review

The existing adapter verifies installed Origin/version, the jail-tested previous
version, root-owned old-package escrow, declared components and upgrade-only pkg
plan **before** stopping a service. It checks health, persists `stopping`, stops
only the BBS allowlist, verifies dedicated-account/PID quiescence, and backs up
data/config/attachments/vault/spool/link/web/doors/binaries/old-packages. It verifies
restored hashes, SQLite integrity and configured SELECTs using copies, boots a
separate unprivileged restored BBS for login/article/mail probes, and verifies the
off-host copy before installation. Start and configured BBS/Web/link/Door probes
follow installation; unrelated packages are compared before and after.

Actual paths cannot be filled from example defaults. Registered Door games require
all wrapper-specific external save paths and startup/return probes. Independently
running game servers require consistent backup integration; they are not killed
as unrelated services. A manually started FreeBSD link process remains a blocker
until reviewed service/PID ownership integration exists. The standard package's
lack of a dedicated link rc script is not repaired by merely listing that service.

No automatic schema-5 conversion is introduced. A release needing a schema change
requires separately reviewed offline copy/restore/migration and cutover evidence.
Post-stop failure persists planned-restore-required and attempts to keep BBS
services stopped. Recovery uses the corresponding old package **and** coherent
DB/config/data backup; binary-only automatic rollback is forbidden. Keep the
failed state and logs, follow the canonical backup/recovery chapter, verify
recovery, then archive failed state with its recovery evidence and obtain a fresh
authorization. Do not delete state to force another deployment.

Apache, SofChat, the FreeBSD OS, shared dependencies and external game services
are outside the helper's allowed operations. No new privileged command is added.

## Initial enablement checklist (not executed)

1. Review/merge this branch after hosted CI. Verify the integrated main's safety
   CI and applicable native/package results through existing workflows.
2. Approve a read-only production inventory: current pkg Origin/version/ABI,
   service users/rc/PIDs/ports, DB schema, active sessions, Web/link, Door wrappers
   and all external save paths, space and old-package availability. Do not export
   credentials or user records to GitHub. Compare the installed version to the
   release's real jail acceptance receipt; revision-only fixtures are insufficient.
3. Supply/verify a different backup machine, dedicated restricted receiver key,
   known-host pin and machine ID; verify restoration before scheduling downtime.
4. Review exact-main helper bytes and actual root-only profile. Separately approve
   installation of helper/public keys/profile and a sudo rule for the argumentless
   helper only (not shell/pkg/service). Restrict SSH forwarding/PTY/agent features;
   use a production-only deploy key, never the native CI keys. A FreeBSD-only key
   may be used for this scope; do not expand its access just to retain all-host CD.
5. Verify production and production-release Environments, main-only policy and
   owner reviewer; configure the release verification public key and separate
   protected approval-signing key. Check authenticated API evidence is readable;
   denied API reads must fail closed. Do not put keys in issues or artifacts.
6. Prepare an approved stable release and its signed manifest from CI candidates.
   Unexpired exact-SHA Actions artifacts are still required by the existing
   verifier. An old asset or tag alone cannot replace expired evidence. Retaining
   durable independently verifiable evidence beyond Actions retention is future
   work; this change does not silently relax that boundary.
7. Run plan. Approve and run FreeBSD-only read-only smoke. Start a fresh apply run,
   review that run's plan/target, and enter that run's approval phrase. If main
   moves or smoke expires, rerun the appropriate steps and obtain fresh approval.
8. Review the audit and application health after the operation. Do not treat SSH
   success as a backup, data preservation or application-health result.

## Validation and documentation evidence

Local scope tests use real Ed25519 signatures and mock GitHub/package/SSH
boundaries. They exercise full controller plan/apply, target isolation, signature
tampering, review/run binding, expired authority/connectivity, missing manifest,
changed tag, failed CI, final upgrade-evidence changes and transport failure audit.
The existing Deployment safety job runs these tests alongside its POSIX fixtures.

Local Windows run: 15 new scope tests, 7 release admission tests and 6 component
tests passed. Existing deployment suite: 31 tests, 23 passed, 4 skipped, 4 errors;
three errors involve POSIX private-directory expectations (including a downstream
transaction-order assertion), and one local socket health probe could not connect.
These are not reported as passing; hosted Linux CI is required for that suite.
The unchanged three-host test in the pre-change checkout also failed the same
private-directory check on Windows. All four modified workflow YAML documents
parse; all 13 extracted shell blocks pass bash syntax validation. The production
workflow event sets remain manual-only. Python compilation and diff whitespace
checks passed.
No unreviewed branch code was sent to the home test machines.

The manual review maps controller connectivity/scope/current-controller checks,
policy authorization membership and transaction traversal to the upgrade chapter
and CI_CD.md. Only those reviewed source-lock entries are refreshed. Generated
HTML/offline bundle, links, redirects, search and publication allowlist are checked
with the existing manual tools: 42 canonical pages, 2,844 local links, Markdown/HTML
parity and 8 JavaScript search cases passed. Production migration, native deployment of this
new scope and actual site backup/restore remain unverified.
