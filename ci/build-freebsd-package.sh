#!/bin/sh
# Build a FreeBSD 15/amd64 release-candidate pkg in a unique, unprivileged CI
# directory from the exact GitHub checkout archive. No installation or reboot.
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: build-freebsd-package.sh CI_WORKDIR SOURCE_SHA SOURCE_TAR_SHA256" >&2
    exit 2
fi

work=$1
revision=$2
source_digest=$3
case "$work" in
    /*/ci/freebsd-pkg-*) ;;
    *) echo "Refusing non-CI work directory" >&2; exit 2 ;;
esac
case "$revision" in
    *[!0-9a-f]*|"") echo "Invalid source SHA" >&2; exit 2 ;;
esac
if [ "$(printf %s "$revision" | wc -c | tr -d ' ')" -ne 40 ] ||
   [ "$(printf %s "$source_digest" | wc -c | tr -d ' ')" -ne 64 ]; then
    echo "Expected full source SHA and 64-character archive digest" >&2
    exit 2
fi
if [ "$(id -u)" -eq 0 ]; then
    echo "Running a ports package builder as root is forbidden" >&2
    exit 2
fi
[ "$(uname -s)" = "FreeBSD" ] && [ "$(uname -m)" = "amd64" ] || {
    echo "The native FreeBSD pkg build requires FreeBSD/amd64" >&2; exit 1;
}
case "$(uname -r)" in
    15.*) ;;
    *) echo "FreeBSD 15.x is required for this candidate" >&2; exit 1 ;;
esac

if [ ! -f /usr/ports/Mk/bsd.port.mk ]; then
    echo "FreeBSD Ports Collection is missing (/usr/ports/Mk/bsd.port.mk)" >&2
    exit 1
fi
for binary in make pkg tar sha256 stat python3; do
    command -v "$binary" >/dev/null 2>&1 || {
        echo "Missing build prerequisite: $binary" >&2; exit 1;
    }
done
pkg -N >/dev/null 2>&1 || {
    echo "The pkg(8) tool must be installed before running CI" >&2
    exit 1
}

archive="$work/source.tar.gz"
[ -f "$archive" ] || { echo "Missing immutable source archive" >&2; exit 1; }
actual=$(sha256 -q "$archive")
[ "$actual" = "$source_digest" ] || {
    echo "Source archive SHA-256 mismatch" >&2; exit 1;
}
port_dir="$work/comms/liner-notes"
if [ -e "$port_dir" ] || [ -e "$work/source" ]; then
    echo "Refusing to reuse a previous build workspace" >&2
    exit 1
fi
mkdir -p "$work/source" "$work/comms" "$work/distfiles" "$work/packages" "$work/artifacts"
tar -xzf "$archive" -C "$work/source"
test -f "$work/source/ports/comms/liner-notes/Makefile"
test -f "$work/source/scripts/release_manifest.py"
# Keep the original category/port directory name: FreeBSD derives PKGORIGIN
# from the port directory and categories, not just the package name.
cp -R "$work/source/ports/comms/liner-notes" "$port_dir"
cp "$archive" "$work/distfiles/liner-notes-stage74.tar.gz"

# Pin Python 3.12 only for this CI port build. The FreeBSD pkg repository
# carries py312-aiohttp/cryptography; do not alter the VM-wide default.
# Derive and verify the flavor before evaluating any dependencies.
python_pkg_prefix=$(make -C "$port_dir" BATCH=yes \
    PYTHON_VERSION=python3.12 OPTIONS_SET="WEBUI LINK ZMODEM" -V PYTHON_PKGNAMEPREFIX)
if [ "$python_pkg_prefix" != "py312-" ]; then
    echo "Unexpected Python package prefix: $python_pkg_prefix (expected py312-)" >&2
    exit 1
fi

# Refuse to publish a pkg with a different origin from the installed port.
# For example, a CI directory named "port" produces "comms/port" and pkg(8)
# then treats 0.74 as a new package instead of upgrading 0.73.a1.
port_origin=$(make -C "$port_dir" BATCH=yes \
    PORTSDIR=/usr/ports PYTHON_VERSION=python3.12 \
    OPTIONS_SET="WEBUI LINK ZMODEM" -V PKGORIGIN)
if [ "$port_origin" != "comms/liner-notes" ]; then
    echo "Unexpected Ports origin: $port_origin (expected comms/liner-notes)" >&2
    exit 1
fi

# Port dependency resolution can descend to dependency "install" targets.
# This verification VM must never have packages auto-installed by CI.
missing=0
check_pkg() {
    name=$1
    min_version=$2
    if ! pkg info -e "$name" >/dev/null 2>&1; then
        echo "MISSING FreeBSD CI prerequisite: $name" >&2
        missing=1
        return
    fi
    if [ -n "$min_version" ]; then
        installed=$(pkg query '%v' "$name") || {
            echo "Cannot query version for $name" >&2
            missing=1
            return
        }
        comparison=$(pkg version -t "$installed" "$min_version") || {
            echo "Cannot compare version for $name" >&2
            missing=1
            return
        }
        if [ "$comparison" = '<' ]; then
            echo "OUTDATED FreeBSD CI prerequisite: $name $installed (need >= $min_version)" >&2
            missing=1
        fi
    fi
}
check_pkg "${python_pkg_prefix}sqlite3" ""
check_pkg "${python_pkg_prefix}aiohttp" "3.11"
check_pkg "${python_pkg_prefix}cryptography" "43"
check_pkg "lrzsz" "0.13.1"
if [ "$missing" -ne 0 ]; then
    echo "CI stopped before stage: install listed prerequisites only on" >&2
    echo "the FreeBSD verification VM 10.0.0.50, then rerun the job." >&2
    echo "No dependency installation or production change was attempted." >&2
    exit 1
fi

version=$(awk -F= '/^DISTVERSION[[:space:]]*=/{gsub(/[[:space:]]/, "", $2);print $2;exit}' "$port_dir/Makefile")
case "$version" in
    ""|*[!0-9.]*) echo "Unrecognized FreeBSD port version" >&2; exit 1 ;;
esac

# The port distfile must be the exact Git archive, never the old v0.74 archive.
make -C "$port_dir" BATCH=yes \
    PORTSDIR=/usr/ports DISTDIR="$work/distfiles" \
    PYTHON_VERSION=python3.12 STRICT_DEPENDS=yes makesum
grep -Fqx "SHA256 (liner-notes-stage74.tar.gz) = $source_digest" "$port_dir/distinfo" || {
    echo "Ports distinfo and Git source archive are inconsistent" >&2; exit 1;
}

# Use the existing ports recipe with WebUI, signed link and ZMODEM enabled.
# Stage and package as an unprivileged account. Never run "make install".
make -C "$port_dir" BATCH=yes \
    PORTSDIR=/usr/ports DISTDIR="$work/distfiles" \
    PACKAGES="$work/packages" OPTIONS_SET="WEBUI LINK ZMODEM" \
    PYTHON_VERSION=python3.12 STRICT_DEPENDS=yes \
    stage check-plist stage-qa package

find "$work/packages" -type f -name 'liner-notes-*.pkg' > "$work/packages-found.txt"
[ "$(wc -l < "$work/packages-found.txt" | tr -d ' ')" -eq 1 ] || {
    echo "Exactly one Liner Notes pkg must be generated" >&2; exit 1;
}
package=$(cat "$work/packages-found.txt")
pkg_name=$(basename "$package")
pkg_version=$(pkg query -F "$package" '%n %v')
[ "$pkg_version" = "liner-notes $version" ] || {
    echo "Generated pkg name/version differs from port" >&2; exit 1;
}
pkg_origin=$(pkg query -F "$package" '%o') || {
    echo "Cannot inspect generated pkg origin" >&2; exit 1;
}
if [ "$pkg_origin" != "$port_origin" ]; then
    echo "Generated pkg origin mismatch: $pkg_origin (expected $port_origin)" >&2
    exit 1
fi
cp "$package" "$work/artifacts/$pkg_name"
python3 "$work/source/scripts/release_manifest.py" create \
    --platform freebsd15 --source-sha "$revision" \
    --version "$version" --directory "$work/artifacts" \
    --package "liner-notes=$pkg_name"
python3 "$work/source/scripts/release_manifest.py" verify \
    --platform freebsd15 --source-sha "$revision" \
    --manifest "$work/artifacts/RELEASE-MANIFEST.json"

printf "source_sha=%s\nplatform=freebsd15\nsource_archive_sha256=%s\n" \
    "$revision" "$source_digest" > "$work/artifacts/BUILD-INFO.txt"
printf "%s  %s\n" "$(sha256 -q "$work/artifacts/$pkg_name")" "$pkg_name" > "$work/artifacts/SHA256SUMS"
echo "PASS: FreeBSD 15 pkg from source commit $revision"
