#!/bin/sh
# Initial test-only setup; refuses replacement of existing different helpers.
set -eu
test "$(id -u)" = 0
test "$#" = 1
exec /usr/local/bin/python3.12 -I ci/install_freebsd_upgrade_helper.py "$1"
