#!/bin/sh
# Explicit root setup on the designated TEST VM; never a package/postinstall hook.
set -eu
test "$#" = 0
test "$(id -u)" = 0
test "$(uname -s)" = FreeBSD
ifconfig -a | grep -E 'inet 10\.0\.0\.50([ /]|$)' >/dev/null
test -x /usr/local/bin/python3.12
test -f ci/selinux_root.py
test -f ci/freebsd_upgrade_root.py
install -d -o root -g wheel -m 755 /usr/local/libexec/liner-notes-ci
install -o root -g wheel -m 644 ci/selinux_root.py /usr/local/libexec/liner-notes-ci/selinux_root.py
install -o root -g wheel -m 755 ci/freebsd_upgrade_root.py /usr/local/libexec/liner-notes-freebsd-upgrade-ci
install -d -o root -g wheel -m 700 /var/db/liner-notes-ci
printf '%s\n' 'Installed reviewed helper only; no sudo rule, jail template, package installation or service operation performed.'
