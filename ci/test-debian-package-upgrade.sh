#!/usr/bin/env bash
# Debian 13 disposable-container-only package-revision upgrade smoke test.
# This is not a production upgrade, BBS schema migration, or restore test.
set -euo pipefail

if [[ "${CI_DISPOSABLE_DEBIAN:-}" != 1 || ! -f /.dockerenv || "$PWD" != /build/liner-notes || "$(id -u)" != 0 ]]; then
  echo 'Refusing to run outside the dedicated disposable Debian CI container' >&2
  exit 2
fi
# shellcheck disable=SC1091
. /etc/os-release
if [[ "$ID" != debian || "$VERSION_ID" != 13 || -d /run/systemd/system ]]; then
  echo 'Expected Debian 13 container without a running systemd instance' >&2
  exit 2
fi

packages=(liner-notes liner-notes-web liner-notes-link)
for package in "${packages[@]}"; do
  test "$(dpkg-query -W -f='${Version}' "$package")" = 0.74-1
done

# Local changes must survive a dpkg revision upgrade without conffile prompts.
config_digests=/tmp/liner-notes-ci-config-digests
: > "$config_digests"
for file in /etc/liner-notes/liner-notes.env /etc/liner-notes/web/web.toml /etc/liner-notes/link.toml; do
  test -f "$file"
  printf '\n# CI-only local customization marker\n' >> "$file"
  sha256sum "$file" >> "$config_digests"
done
mkdir -p /var/lib/liner-notes /var/lib/liner-notes-link
printf 'retain-me\n' > /var/lib/liner-notes/package-removal-test
printf 'CI-ONLY-SYNTHETIC-LINK-KEY\n' > /var/lib/liner-notes-link/ci-link-key
chmod 0600 /var/lib/liner-notes-link/ci-link-key
sha256sum /var/lib/liner-notes-link/ci-link-key >> "$config_digests"

# Create an isolated SQLite fixture; never overwrite the real BBS database.
python3 - <<'PY'
import sqlite3
with sqlite3.connect('/var/lib/liner-notes/ci-package-upgrade.sqlite') as db:
    db.execute('CREATE TABLE ci_marker (value TEXT NOT NULL)')
    db.execute('INSERT INTO ci_marker VALUES (?)', ('kept-across-upgrade',))
PY

# Build a second Debian revision of the same source, inside this container.
# Original 0.74-1 artifacts in /out remain unchanged and are the only
# versioned candidates uploaded by the release manifest workflow.
cat > /tmp/liner-notes-ci-changelog <<'EOF'
liner-notes (0.74-2) unstable; urgency=medium

  * CI-only disposable packaging-revision upgrade rehearsal.

 -- Liner Notes CI <34602508+matsuotakeshi@users.noreply.github.com>  Sun, 08 Nov 2026 00:00:00 +0000
EOF
cat debian/changelog >> /tmp/liner-notes-ci-changelog
mv /tmp/liner-notes-ci-changelog debian/changelog
dpkg-buildpackage -b -us -uc
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  /build/liner-notes_0.74-2_amd64.deb \
  /build/liner-notes-web_0.74-2_all.deb \
  /build/liner-notes-link_0.74-2_all.deb

for package in "${packages[@]}"; do
  test "$(dpkg-query -W -f='${Version}' "$package")" = 0.74-2
done
sha256sum -c "$config_digests"
test "$(cat /var/lib/liner-notes/package-removal-test)" = retain-me
python3 - <<'PY'
import sqlite3
with sqlite3.connect('/var/lib/liner-notes/ci-package-upgrade.sqlite') as db:
    assert db.execute('PRAGMA integrity_check').fetchone() == ('ok',)
    assert db.execute('SELECT value FROM ci_marker').fetchone() == ('kept-across-upgrade',)
PY
echo 'PASS: isolated Debian 13 same-source revision 0.74-1 -> 0.74-2, conffiles, SQLite fixture and link-key retained'
