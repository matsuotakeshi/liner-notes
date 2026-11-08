#!/bin/sh
# Read-only architecture/OS target probe; streamed to ln-deploy via SSH.
# Does not inspect private files, stop processes, install or change anything.
set -eu
if [ "$#" -ne 1 ]; then
  echo 'Expected exactly one release target' >&2
  exit 2
fi
target=$1
os=$(uname -s)
arch=$(uname -m)
case "$target" in
  freebsd15)
    [ "$os" = "FreeBSD" ] && [ "$arch" = "amd64" ] || {
      echo 'FreeBSD production release target or architecture differs' >&2
      exit 1
    }
    case "$(uname -r)" in
      15.*) ;;
      *) echo 'FreeBSD production major release is not 15' >&2; exit 1 ;;
    esac
    ;;
  rocky10|debian13)
    [ "$os" = "Linux" ] && [ "$arch" = "x86_64" ] || {
      echo 'Linux production target or architecture differs' >&2
      exit 1
    }
    [ -r /etc/os-release ] || {
      echo 'Cannot read /etc/os-release on production target' >&2
      exit 1
    }
    # /etc/os-release is owned by the OS; source it only in a subshell.
    (
      . /etc/os-release
      case "$target" in
        rocky10)
          [ "${ID:-}" = rocky ] || exit 1
          case "${VERSION_ID:-}" in 10|10.*) ;; *) exit 1 ;; esac
          ;;
        debian13)
          [ "${ID:-}" = debian ] || exit 1
          case "${VERSION_ID:-}" in 13|13.*) ;; *) exit 1 ;; esac
          ;;
      esac
    ) || { echo 'Linux distribution/major release does not match the candidate' >&2; exit 1; }
    ;;
  *)
    echo 'Unknown release target' >&2
    exit 2
    ;;
esac
printf 'PASS: %s release target OS and architecture match\n' "$target"
