# Cross-OS release-candidate gate

This gate **does not deploy anything**. It checks that prebuilt FreeBSD
15, Rocky Linux 10, and Debian 13 artifacts originate from the same
trusted main commit SHA and same base version. Each artifact directory
must have RELEASE-MANIFEST.json, SHA256SUMS, BUILD-INFO.txt and exact
listed binary packages.

The verifier checks required package counts and architectures,
SHA-256 hashes, redundant checksums, build source SHA and base versions.
It rejects wrong source revisions, corrupted/missing packages and
unsafe paths. The manifests are not signed: the operator still must
authenticate each source GitHub Actions run, and successful packaging
alone does not verify data upgrade or application behavior.

Usage after downloading verified workflow artifacts:

    python3 scripts/verify_release_set.py \
      --root /path/to/extracted/candidates \
      --source-sha FULL_MAIN_COMMIT_SHA \
      --output /path/to/new/RELEASE-SET.json

Directory layout:

    candidates/freebsd15/{liner-notes*.pkg, RELEASE-MANIFEST.json, SHA256SUMS, BUILD-INFO.txt}
    candidates/rocky10/{liner-notes*.rpm, RELEASE-MANIFEST.json, SHA256SUMS, BUILD-INFO.txt}
    candidates/debian13/{liner-notes*.deb, RELEASE-MANIFEST.json, SHA256SUMS, BUILD-INFO.txt}

The GitHub workflow "Release Set Verification" triggers automatically on
every main push and can also be run manually against current main HEAD.
It waits for the independent platform CI jobs (with a strict timeout),
rejects missing/failed runs and verifies the exact same SHA. It downloads
candidates from separate successful *main push* runs for the exact
main HEAD, validates them, and publishes RELEASE-SET.json plus authenticated-source-run references
(RELEASE-SOURCES.txt) as Actions artifacts. No deployment credentials are loaded.

Before turning on CD, test real package upgrades and rollbacks, database
and BBS link protocol compatibility, and confirm the live hosts' OS and
current installations read-only. The FreeBSD production host may not
have Apache, SofChat or its OS restarted.
