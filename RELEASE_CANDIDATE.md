# Release candidate artifacts

This phase builds RPM and DEB candidates in isolated GitHub-hosted Linux
containers, and performs checksum and provenance checks. It does not publish
GitHub Releases, access production hosts, deploy, restart or update services.

Each candidate artifact set carries:
- Exact source commit SHA (the GitHub checkout commit).
- Package target and version, binary file sizes, SHA-256 digests.
- RELEASE-MANIFEST.json, SHA256SUMS, BUILD-INFO.txt.

Inspect after obtaining an artifact set:

    python3 scripts/release_manifest.py verify \
      --manifest RELEASE-MANIFEST.json --platform rocky10 \
      --source-sha FULL_40_CHARACTER_COMMIT_SHA

The manifest is not a cryptographic signature. Its integrity must be anchored
in a trusted GitHub run and authenticated CI artifacts. Source provenance
alone is not proof that a build is reproducible.

FreeBSD pkg generation from the *same commit* has a main-only candidate
workflow, but its actual CI result and OS prerequisites still require verification.
Package upgrade, backup/restore, and BBS link rolling-version tests remain pending. In particular, the existing FreeBSD port distinfo
pins a versioned upstream release archive rather than a chosen Git commit;
do not silently use it as if it matched an arbitrary release commit.

Do not update any of rocky.ysni.org, debian.ysni.org or srv.ysni.org from
these candidate artifacts. The production FreeBSD host also runs SofChat,
with in-memory chat history: its OS, Apache and SofChat must never be
automatically stopped or restarted.

## FreeBSD pkg candidate workflow

The FreeBSD 15 Package CI workflow is main-push/manual-only and is never
triggered by pull requests. A separate FreeBSD Package Source Checks job
runs on GitHub-hosted infrastructure for PR safety.

The trusted self-hosted runner creates a source archive from the exact
GitHub main commit and checks its SHA-256. The archive is sent by an SSH
CI-only account to 10.0.0.50, the home FreeBSD 15/amd64 verification VM.

The build script requires pkg(8), Python 3 and the Ports Collection in
/usr/ports. It copies the existing port skeleton into a unique CI-owned
directory, replaces distinfo there only with the checksum of the same
Git archive, then stages and packages with WEBUI, LINK and ZMODEM enabled.
All work runs without root and does not install packages or restart services.

The candidate package is checked with pkg query and then SHA-256
verified against the source revision in RELEASE-MANIFEST.json.
Missing prerequisites or a package build failure stop the workflow.

The resulting pkg is an Actions artifact, not a production release.
Production FreeBSD srv.ysni.org is not contacted by this workflow, and
OS / Apache / SofChat must never be restarted by any automated job.

The workflow must still succeed on a trusted main commit before this
capability can be considered completed. Package upgrade and rollback
checks remain additional blockers for production deployment.
