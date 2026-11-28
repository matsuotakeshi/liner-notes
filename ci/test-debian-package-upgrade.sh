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
version=$(dpkg-parsechangelog -S Version)
case "$version" in *-1) next_version="${version%-1}-2" ;; *) exit 1 ;; esac
/usr/bin/liner-notes-doctor --check-doors --lang en --json > /tmp/liner-notes-doctor.json
python3 -c 'import json; r=json.load(open("/tmp/liner-notes-doctor.json")); assert r["read_only"] and r["exit_code"] == 0'
for package in "${packages[@]}"; do
  test "$(dpkg-query -W -f='${Version}' "$package")" = "$version"
done

# Local changes must survive a dpkg revision upgrade without conffile prompts.
apparmor_digests=/tmp/liner-notes-apparmor-local-digests
install -d /etc/apparmor.d/local /etc/systemd/system/liner-notes.service.d
printf '# Local AppArmor rules retained by upgrade and purge\n' > /etc/apparmor.d/local/liner-notes
cp /usr/share/liner-notes-apparmor/systemd/liner-notes.service.conf \
  /etc/systemd/system/liner-notes.service.d/90-apparmor.conf
sha256sum /etc/apparmor.d/local/liner-notes /etc/systemd/system/liner-notes.service.d/90-apparmor.conf > "$apparmor_digests"

config_digests=/tmp/liner-notes-ci-config-digests
: > "$config_digests"
for file in /etc/liner-notes/liner-notes.env /etc/liner-notes/web/web.toml /etc/liner-notes/link.toml; do
  test -f "$file"
  printf '\n# CI-only local customization marker\n' >> "$file"
  sha256sum "$file" >> "$config_digests"
done
test "$(stat -c '%U:%G %a' /var/lib/liner-notes-link)" = 'liner_notes_link:liner_notes_linkctl 700'
manual_unit=/etc/systemd/system/liner-notes-link.service
mkdir -p /etc/systemd/system
printf '# Local service override retained by package upgrades\n' > "$manual_unit"
sha256sum "$manual_unit" >> "$config_digests"
chmod 0644 /etc/liner-notes/link.toml
config_metadata=$(stat -c '%u:%g %a' /etc/liner-notes/link.toml)
state_metadata=$(stat -c '%u:%g %a' /var/lib/liner-notes-link)
mkdir -p /var/lib/liner-notes /var/lib/liner-notes-link
printf 'keep\n' > /var/lib/liner-notes/package-removal-test
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
# Original artifacts in /out remain unchanged and are the only
# versioned candidates uploaded by the release manifest workflow.
cat > /tmp/liner-notes-ci-changelog <<EOF
liner-notes ($next_version) unstable; urgency=medium

  * CI-only disposable packaging-revision upgrade rehearsal.

 -- Liner Notes CI <34602508+matsuotakeshi@users.noreply.github.com>  Sun, 08 Nov 2026 00:00:00 +0000
EOF
cat debian/changelog >> /tmp/liner-notes-ci-changelog
mv /tmp/liner-notes-ci-changelog debian/changelog
dpkg-buildpackage -b -us -uc
python3 ci/test-deploy-package-adapter.py debian13 \
  "/build/liner-notes_${next_version}_amd64.deb" \
  "/build/liner-notes-web_${next_version}_all.deb" \
  "/build/liner-notes-link_${next_version}_all.deb" \
  "/build/liner-notes-apparmor_${next_version}_all.deb"
test "$(dpkg-query -W -f='${Version}' liner-notes-apparmor)" = "$next_version"
sha256sum -c "$apparmor_digests"

for package in "${packages[@]}"; do
  test "$(dpkg-query -W -f='${Version}' "$package")" = "$next_version"
done
sha256sum -c "$config_digests"
test "$(stat -c '%u:%g %a' /etc/liner-notes/link.toml)" = "$config_metadata"
test "$(stat -c '%u:%g %a' /var/lib/liner-notes-link)" = "$state_metadata"
test "$(cat /var/lib/liner-notes/package-removal-test)" = keep
python3 - <<'PY'
import sqlite3
with sqlite3.connect('/var/lib/liner-notes/ci-package-upgrade.sqlite') as db:
    assert db.execute('PRAGMA integrity_check').fetchone() == ('ok',)
    assert db.execute('SELECT value FROM ci_marker').fetchone() == ('kept-across-upgrade',)
PY
echo "PASS: isolated Debian 13 same-source revision $version -> $next_version, conffiles, SQLite fixture and link-key retained"
