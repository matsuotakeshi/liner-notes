#!/usr/bin/env bash
# Disposable Rocky Linux 10 RPM revision upgrade; never run on a real host.
# No systemd is active. This does not prove running-service upgrade safety.
set -euo pipefail

if [[ "${CI_DISPOSABLE_ROCKY:-}" != 1 || ! -f /.dockerenv ||
      "$PWD" != /build/liner-notes || "$(id -u)" != 0 ]]; then
  echo 'Refusing to run outside the dedicated disposable Rocky CI container' >&2
  exit 2
fi
# shellcheck disable=SC1091
. /etc/os-release
if [[ "$ID" != rocky || "$VERSION_ID" != 10* || -d /run/systemd/system ]]; then
  echo 'Expected Rocky Linux 10 disposable container without running systemd' >&2
  exit 2
fi

packages=(liner-notes liner-notes-web liner-notes-link)
dnf install -y --setopt=install_weak_deps=False epel-release
dnf install -y --setopt=install_weak_deps=False \
  /out/liner-notes-0.74.1-1*.x86_64.rpm \
  /out/liner-notes-web-0.74.1-1*.x86_64.rpm \
  /out/liner-notes-link-0.74.1-1*.x86_64.rpm
for package in "${packages[@]}"; do
  rpm -q --qf '%{VERSION}-%{RELEASE}\n' "$package" | grep -Eq '^0\.74\.1-1(\.|$)'
done

# The RPM %config(noreplace) entries must preserve local modifications.
config_hashes=/tmp/liner-notes-rpm-config-hashes
: > "$config_hashes"
for file in \
    /etc/liner-notes/liner-notes.env \
    /etc/liner-notes/web/web.toml \
    /etc/liner-notes/link.toml \
    /usr/share/liner-notes/banner.txt; do
  test -f "$file"
  printf '\n# CI-only customization marker\n' >> "$file"
  sha256sum "$file" >> "$config_hashes"
done

# Synthetic test data and key only. No production keys are used.
mkdir -p /var/lib/liner-notes /var/lib/liner-notes-link
printf 'keep\n' > /var/lib/liner-notes/package-removal-test
printf 'CI-ONLY-SYNTHETIC-LINK-KEY\n' > /var/lib/liner-notes-link/ci-link-key
chmod 0600 /var/lib/liner-notes-link/ci-link-key
sha256sum /var/lib/liner-notes-link/ci-link-key >> "$config_hashes"
python3 - <<'PY'
import sqlite3
with sqlite3.connect("/var/lib/liner-notes/ci-package-upgrade.sqlite") as db:
    db.execute("CREATE TABLE ci_marker (value TEXT NOT NULL)")
    db.execute("INSERT INTO ci_marker VALUES ('kept-across-rpm-upgrade')")
PY

# Only in this throwaway filesystem: bump RPM release, not app version.
python3 - <<'PY'
from pathlib import Path
path = Path("linux/rpm/liner-notes.spec")
old = "Release:        1%{?dist}"
text = path.read_text(encoding="utf-8")
if text.count(old) != 1:
    raise SystemExit("Unexpected RPM release definition")
path.write_text(text.replace(old, "Release:        2%{?dist}"), encoding="utf-8")
PY
python3 scripts/build-rpm.py /build/rpm-upgrade-work

# Test the three packages as one version-coupled dependency set.
upgrade_packages=()
while IFS= read -r -d '' filename; do
  name="$(rpm -qp --qf '%{NAME}' "$filename")"
  case "$name" in
    liner-notes|liner-notes-web|liner-notes-link)
      upgrade_packages+=("$filename")
      ;;
  esac
done < <(find /build/rpm-upgrade-work/RPMS -type f -name '*.rpm' -print0)
test "${#upgrade_packages[@]}" -eq 3
python3 ci/test-deploy-package-adapter.py rocky10 "${upgrade_packages[@]}"

for package in "${packages[@]}"; do
  rpm -q --qf '%{VERSION}-%{RELEASE}\n' "$package" | grep -Eq '^0\.74\.1-2(\.|$)'
done
sha256sum -c "$config_hashes"
test "$(cat /var/lib/liner-notes/package-removal-test)" = keep
python3 - <<'PY'
import sqlite3
with sqlite3.connect("/var/lib/liner-notes/ci-package-upgrade.sqlite") as db:
    assert db.execute("PRAGMA integrity_check").fetchone() == ("ok",)
    assert db.execute("SELECT value FROM ci_marker").fetchone() == ("kept-across-rpm-upgrade",)
PY

dnf remove -y --setopt=install_weak_deps=False "${packages[@]}"
test "$(cat /var/lib/liner-notes/package-removal-test)" = keep
test -s /var/lib/liner-notes-link/ci-link-key
test -s /var/lib/liner-notes/ci-package-upgrade.sqlite
echo 'PASS: disposable Rocky 10 RPM 0.74.1-1 -> 0.74.1-2, modified configs, SQLite and synthetic link-key preserved'
