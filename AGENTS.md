# Documentation policy

For every change to Liner Notes, follow `docs/CONTRIBUTING.md`.
The canonical public manual is the versioned, seven-chapter book under
`docs/manual/`. Update the relevant functional chapters with changes to commands,
permissions, settings, limits, installation, operation and recovery.
Do not publish development-stage reports as user documentation. Keep development
records under `docs/development/` or `evidence/`, and concise changes in CHANGELOG.
Validate Markdown/HTML/offline parity, all internal links, legacy URL mappings and
the publication allowlist before delivery. Do not change runtime functionality,
configuration or database structures merely to reconcile documentation.

# Development and CI/CD policy

For every version, feature, fix, port and package change, follow the permanent
policy in `CI_CD.md` and the development flow in `docs/CONTRIBUTING.md`.
Reuse the existing GitHub Actions workflows, runner, SSH keys, verified host
keys and CI accounts. Manual checks supplement CI; they never replace it.
Work on a branch and open a PR. Require successful applicable GitHub-hosted
checks before merging reviewed code to main, then verify the exact main commit
with the existing FreeBSD, Rocky Linux and Ubuntu native CI. Investigate failures;
do not disable tests or weaken conditions to obtain a passing result.
Documentation-only changes use the shared CI scope classifier: keep Canonical
manual and lightweight checks, but do not build binaries/packages or contact
native VMs. Record those checks as not applicable, never as tests executed.
Mixed/unknown changes and manual dispatch still require full verification.
See the documentation-only exception in `CI_CD.md`.

Self-hosted runners and home test hosts must execute trusted main code only.
Never run unreviewed PR code against the home network or production. Manual
workflow dispatch is also restricted to main; inspect the selected ref before
dispatching. Do not rebuild runner/VM infrastructure unnecessarily.

Use CI-produced release candidates and verify source SHA, version and SHA-256
for pkg/RPM/DEB; keep FreeBSD Origin `comms/liner-notes`. Preserve evidence in
Actions logs/artifacts. Production is not a test environment. Package generation
and GitHub release publication do not authorize deployment. Production updates
require explicit approval, required CI, preflight and verified backup/restore.
CI/CD must never stop, restart or modify the FreeBSD OS, Apache or SofChat on
`srv.ysni.org`. See `CI_CD.md` for all protected production hosts and test roles.
