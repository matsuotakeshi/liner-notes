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
import importlib.util, re, subprocess, sys
spec = importlib.util.spec_from_file_location('acceptance_helper', sys.argv[1])
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
assert 'liner_notes' in helper.installed_modules(), 'Native helper cannot list the real policy store'
unconfined = subprocess.check_output(['seinfo', '-a', 'unconfined_domain_type', '-x'], text=True)
assert not any('liner_notes' in row for row in unconfined.splitlines()), unconfined
# Reject attribute inheritance even when its conditional allow is disabled on
# this host. Keep systemd entrypoint and NoNewPrivileges transitions working.
daemons = subprocess.check_output(['seinfo', '-a', 'daemon', '-x'], text=True)
resource_types = subprocess.check_output(['seinfo', '-a', 'sys_resource_type', '-x'], text=True)
for role in ('broker', 'isolation', 'consumer'):
    domain = 'liner_notes_' + role + '_t'
    entry = 'liner_notes_' + role + '_exec_t'
    assert not re.search(r'\b' + domain + r'\b', daemons), (domain, daemons)
    assert not re.search(r'\b' + domain + r'\b', resource_types), (domain, resource_types)
    resources = subprocess.check_output(['sesearch', '--allow', '-s', domain,
                                        '-c', 'capability', '-p', 'sys_resource'], text=True)
    assert not resources.strip(), (domain, resources)
    interpreter = subprocess.check_output(['sesearch', '--allow', '-s', domain,
                                          '-t', 'bin_t', '-c', 'file', '-p', 'execute_no_trans'], text=True)
    assert interpreter.strip(), (domain, interpreter)
    transitions = subprocess.check_output(['sesearch', '--type_trans', '-s', 'init_t',
                                           '-t', entry, '-c', 'process'], text=True)
    assert re.search(r'\b' + domain + r'\s*;', transitions), (domain, transitions)
    nnp = subprocess.check_output(['sesearch', '--allow', '-s', 'init_t',
                                  '-t', domain, '-c', 'process2', '-p', 'nnp_transition'], text=True)
    assert nnp.strip(), (domain, nnp)
    for permission, expected in [('getattr', True), ('read', True), ('write', True),
                                  ('connectto', False), ('setopt', False)]:
        rules=subprocess.check_output(['sesearch','--allow','-s',domain,'-t','init_t',
                                       '-c','unix_stream_socket','-p',permission],text=True)
        assert bool(rules.strip()) == expected, (domain,permission,rules)
for permission, expected in [('getopt', True), ('setopt', False), ('connectto', False)]:
    rules = subprocess.check_output(['sesearch', '--allow', '-s', 'liner_notes_transfer_t',
                                     '-t', 'liner_notes_t', '-c', 'unix_stream_socket',
                                     '-p', permission], text=True)
    assert bool(rules.strip()) == expected, (permission, rules)
# Process-state enumeration must not imply ptrace, signalling unrelated services
# or modification of their process-labelled files. Existing BBS-data denial
# checks below remain mandatory.
for target, kind, permission, expected in [
    ('init_t', 'dir', 'search', True),
    ('init_t', 'file', 'read', True),
    ('init_t', 'file', 'write', False),
    ('init_t', 'lnk_file', 'read', False),
    ('init_t', 'process', 'ptrace', False),
    ('init_t', 'process', 'signal', False),
    ('liner_notes_isolation_t', 'capability', 'sys_ptrace', False),
    ('liner_notes_isolation_t', 'capability', 'sys_admin', False),
]:
    rules=subprocess.check_output(['sesearch','--allow','-s','liner_notes_isolation_t',
                                   '-t',target,'-c',kind,'-p',permission],text=True)
    assert bool(rules.strip()) == expected, (target,kind,permission,rules)
for domain, target in [('liner_notes_web_t', 'liner_notes_data_t'),
                       ('liner_notes_web_t', 'liner_notes_link_secret_t'),
                       ('liner_notes_door_t', 'liner_notes_data_t'),
                       ('liner_notes_transfer_t', 'liner_notes_data_t'),
                       ('liner_notes_broker_t', 'liner_notes_data_t'),
                       ('liner_notes_isolation_t', 'liner_notes_data_t'),
                       ('liner_notes_consumer_t', 'liner_notes_data_t'),
                       ('liner_notes_door_t', 'liner_notes_isolation_state_t')]:
    rules = subprocess.check_output(['sesearch', '--allow', '-s', domain,
                                     '-t', target, '-c', 'file', '-p', 'read'], text=True)
    assert not rules.strip(), (domain, target, rules)
# Inspect the compiled policy, including conditional/broad attribute grants.
for domain in ('liner_notes_t', 'liner_notes_admin_t', 'liner_notes_web_t',
               'liner_notes_link_t', 'liner_notes_door_t', 'liner_notes_transfer_t',
               'liner_notes_broker_t', 'liner_notes_isolation_t', 'liner_notes_consumer_t'):
    rules = subprocess.check_output(['sesearch', '--allow', '-s', domain,
                                    '-t', 'liner_notes_ssh_secret_t', '-c', 'file', '-p', 'read'], text=True)
    assert bool(rules.strip()) == (domain in ('liner_notes_t', 'liner_notes_admin_t')), (domain, rules)
    rules = subprocess.check_output(['sesearch', '--allow', '-s', domain,
                                    '-t', 'liner_notes_ssh_port_t', '-c', 'tcp_socket', '-p', 'name_bind'], text=True)
    assert bool(rules.strip()) == (domain == 'liner_notes_t'), (domain, rules)
    for permission in ('write', 'create', 'unlink'):
        rules = subprocess.check_output(['sesearch', '--allow', '-s', domain,
                                        '-t', 'liner_notes_ssh_secret_t', '-c', 'file', '-p', permission], text=True)
        assert not rules.strip(), (domain, permission, rules)
for source, target, expected in [
    ('liner_notes_isolation_t','liner_notes_broker_t',True),
    ('liner_notes_consumer_t','liner_notes_broker_t',True),
    ('liner_notes_t','liner_notes_isolation_t',True),
    ('liner_notes_door_t','liner_notes_consumer_t',True),
    ('liner_notes_t','liner_notes_broker_t',False),
    ('liner_notes_web_t','liner_notes_broker_t',False),
    ('liner_notes_link_t','liner_notes_broker_t',False),
    ('liner_notes_door_t','liner_notes_broker_t',False),
    ('liner_notes_door_t','liner_notes_isolation_t',False),
    ('liner_notes_consumer_t','liner_notes_isolation_t',False),
]:
    rules=subprocess.check_output(['sesearch','--allow','-s',source,'-t',target,
                                  '-c','unix_stream_socket','-p','connectto'],text=True)
    assert bool(rules.strip()) == expected, (source,target,rules)
for domain in ('liner_notes_broker_t','liner_notes_consumer_t'):
    rules=subprocess.check_output(['sesearch','--allow','-s',domain,'-c','capability'],text=True)
    assert not rules.strip(), (domain,rules)

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
