#!/bin/bash
# Only the disposable hosted Rocky container; no services/host network involved.
set -euo pipefail
test "${CI_SELINUX_DISPOSABLE:-}" = 1
test "$#" = 2
rpmfile="$1"
updated="$2"
rpm -U "$rpmfile"
semodule -lfull | grep -E '^200[[:space:]]+liner_notes[[:space:]]'
# Inspect the compiled allow graph rather than treating an unconfined run as MAC.
source_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
python3 - "$source_root/ci/selinux_root.py" <<'PY'
import importlib.util, subprocess, sys
spec = importlib.util.spec_from_file_location('acceptance_helper', sys.argv[1])
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
assert 'liner_notes' in helper.installed_modules(), 'Native helper cannot list the real policy store'
unconfined = subprocess.check_output(['seinfo', '-a', 'unconfined_domain_type', '-x'], text=True)
assert not any('liner_notes' in row for row in unconfined.splitlines()), unconfined
for permission, expected in [('getopt', True), ('setopt', False), ('connectto', False)]:
    rules = subprocess.check_output(['sesearch', '--allow', '-s', 'liner_notes_transfer_t',
                                     '-t', 'liner_notes_t', '-c', 'unix_stream_socket',
                                     '-p', permission], text=True)
    assert bool(rules.strip()) == expected, (permission, rules)
for domain, target in [('liner_notes_web_t', 'liner_notes_data_t'),
                       ('liner_notes_web_t', 'liner_notes_link_secret_t'),
                       ('liner_notes_door_t', 'liner_notes_data_t'),
                       ('liner_notes_transfer_t', 'liner_notes_data_t')]:
    rules = subprocess.check_output(['sesearch', '--allow', '-s', domain,
                                     '-t', target, '-c', 'file', '-p', 'read'], text=True)
    assert not rules.strip(), (domain, target, rules)
PY
semanage fcontext -a -t var_lib_t '/var/lib/liner-notes-local-policy-fixture(/.*)?'
semanage fcontext -l -C > /tmp/labels-before
# Installing the same artifact is an idempotent package reinstall, not an
# old-to-new compatibility claim. A real RPM release update follows.
rpm -U --replacepkgs "$rpmfile"
rpm -U "$updated"
test "$(rpm -q --qf '%{RELEASE}' liner-notes-selinux)" != "$(rpm -qp --qf '%{RELEASE}' "$rpmfile")"
semanage fcontext -l -C > /tmp/labels-after
cmp /tmp/labels-before /tmp/labels-after
rpm -e liner-notes-selinux
if semodule -lfull | grep -Eq '^200[[:space:]]+liner_notes[[:space:]]'; then exit 1; fi
semanage fcontext -l -C > /tmp/labels-after
cmp /tmp/labels-before /tmp/labels-after
semanage fcontext -d '/var/lib/liner-notes-local-policy-fixture(/.*)?'
echo 'PASS offline policy graph and RPM install/reinstall/update/remove; Enforcing runtime not tested here'
