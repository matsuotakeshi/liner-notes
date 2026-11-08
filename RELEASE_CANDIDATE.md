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

FreeBSD pkg generation from the *same commit* is still a prerequisite for a
complete three-OS release, as are package upgrade, backup/restore, and BBS link
rolling-version tests. In particular, the existing FreeBSD port distinfo
pins a versioned upstream release archive rather than a chosen Git commit;
do not silently use it as if it matched an arbitrary release commit.

Do not update any of rocky.ysni.org, debian.ysni.org or srv.ysni.org from
these candidate artifacts. The production FreeBSD host also runs SofChat,
with in-memory chat history: its OS, Apache and SofChat must never be
automatically stopped or restarted.
