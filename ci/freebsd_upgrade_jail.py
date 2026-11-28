#!/usr/bin/env python3
"""Actual pkg/account/service acceptance; refuses to execute outside its jail."""
import asyncio
import hashlib
import json
import os
from pathlib import Path
import pwd
import shlex
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = Path('/ci/source')
sys.path[:0] = [str(ROOT / 'ci'), str(ROOT / 'scripts'), str(ROOT / 'tests'), str(ROOT)]
from deploy_policy import package_commands
from deploy_restore import verify_application
from deploy_host import trusted
from test_board import Server, post
from test_runtime import Client
from test_members import auth, PASSWORD
from test_mail import send
from package_menu_flow import readback, restore_command
from freebsd_upgrade_root import write_door_fixture
from freebsd_fixture_permissions import package_install_argv, repair_legacy_sbin

PHASE = 'admission'

ENV = {'PATH': '/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin', 'LC_ALL': 'C', 'HOME': '/root'}


def command(argv, *, env=ENV, timeout=180, user=None):
    extra = {} if user is None else {'user': user.pw_uid, 'group': user.pw_gid, 'extra_groups': []}
    result = subprocess.run(list(map(str, argv)), env=env, capture_output=True, text=True, timeout=timeout, **extra)
    if result.returncode:
        Path('/ci/failure.log').write_text(result.stdout + result.stderr)
        raise AssertionError('Isolated command failed: ' + str(argv[0]))
    return result.stdout.strip()


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def link_service_check():
    """Run the packaged rc.d in the disposable jail with synthetic keys."""
    import grp
    import link_transport

    account = pwd.getpwnam('liner_notes_link')
    group = grp.getgrnam('liner_notes_linkctl')
    assert account.pw_uid > 0 and account.pw_gid == group.gr_gid
    state = Path('/var/db/liner-notes-link')
    metadata = state.lstat()
    mode = stat.S_IMODE(metadata.st_mode)
    assert stat.S_ISDIR(metadata.st_mode) and (metadata.st_uid, metadata.st_gid, mode) == (
        account.pw_uid, group.gr_gid, 0o700
    ), f'Link state directory: uid={metadata.st_uid} gid={metadata.st_gid} mode={mode:04o}'
    rc = Path('/usr/local/etc/rc.d/liner_notes_link')
    vendor = Path('/usr/local/share/liner-notes/liner_notes_link.sample')
    assert rc.read_bytes() == vendor.read_bytes() and os.access(rc, os.X_OK)
    assert 'liner_notes_link_enable="NO"' in command([rc, 'rcvar']).splitlines()
    with tempfile.TemporaryDirectory(prefix='link-rc-', dir=state) as temp:
        directory = Path(temp)
        identity = directory/'identity'
        node = link_transport.generate(identity, 'FreeBSD CI node')
        peer = directory/'peer'
        link_transport.generate(peer, 'FreeBSD CI offline peer')
        secret = directory/'password'
        secret.write_text('disposable-unregistered-link-member\n')
        secret.chmod(0o600)
        cfg = directory/'link.toml'
        use_port = free_port()
        bbs_port = free_port()
        peer_port = free_port()
        cfg.write_text(f"""[node]
identity = "{identity}"
database = "{directory/'events.sqlite'}"
port = {use_port}
control_socket = "/var/run/liner-notes-link/control.sock"
[bbs]
port = {bbs_port}
login = "fixturelink"
password_file = "{secret}"
[[boards]]
id = "{'1'*64}"
local = 1
[[origins]]
certificate = "{peer/'identity.crt'}"
boards = ["{'1'*64}"]
[[peers]]
certificate = "{peer/'identity.crt'}"
boards = ["{'1'*64}"]
host = "127.0.0.1"
port = {peer_port}
""")
        for item in [directory, *directory.rglob('*')]:
            os.chown(item, account.pw_uid, group.gr_gid)
        command(['/usr/sbin/sysrc', f'liner_notes_link_config={cfg}'])
        wrapper = '/usr/local/bin/liner-notes-link'
        pidfile = Path('/var/run/liner_notes_link.pid')
        control_path = Path('/var/run/liner-notes-link/control.sock')

        def ready(previous=None):
            for _ in range(100):
                if pidfile.is_file() and control_path.is_socket():
                    pid = pidfile.read_text().strip()
                    if not pid or pid == previous:
                        time.sleep(.1)
                        continue
                    result = json.loads(command([wrapper, 'status', '--config', cfg], user=account))
                    assert result['node'] == node
                    children = command(['/bin/ps', '-o', 'pid=,ppid=,uid=', '-ax']).splitlines()
                    child, = [p for p, ppid, uid in map(str.split, children)
                              if ppid == pid and int(uid) == account.pw_uid]
                    return pid, child
                time.sleep(.1)
            raise AssertionError('Packaged link rc.d did not become ready')

        command([wrapper, 'check', '--config', cfg], user=account)
        try:
            command([rc, 'onestart'], timeout=15)
            pid, child = ready()
            assert pid in command([rc, 'onestatus'])
            duplicate = subprocess.run([rc, 'onestart'], env=ENV,
                                       capture_output=True, text=True, timeout=15)
            assert duplicate.returncode == 1 and pid in duplicate.stderr
            assert ready() == (pid, child)
            command([rc, 'onerestart'], timeout=90)
            restarted, new_child = ready(pid)
            assert (restarted, new_child) != (pid, child)
        finally:
            command([rc, 'onestop'], timeout=70)
        assert not pidfile.exists() and not control_path.exists()
        for dead_pid in (restarted, new_child):
            assert subprocess.run(['/bin/ps', '-p', dead_pid], env=ENV, capture_output=True).returncode != 0
        assert json.loads(command([wrapper, 'status', '--config', cfg], user=account))['node'] == node




def main():
    global PHASE
    assert os.geteuid() == 0 and sys.platform.startswith('freebsd')
    assert command(['/sbin/sysctl', '-n', 'security.jail.jailed']) == '1'
    assert command(['/bin/hostname']) == 'liner-ci.invalid'
    assert Path(__file__).resolve() == ROOT / 'ci/freebsd_upgrade_jail.py'
    sha = sys.argv[1]
    manifest = json.loads(Path('/ci/packages/RELEASE-MANIFEST.json').read_text())
    assert manifest['source_sha'] == sha
    profile = json.loads(Path('/ci/template.json').read_text())
    pkg = '/usr/local/sbin/pkg'
    before = command([pkg, 'query', '%n %v %o']).splitlines()
    assert command([pkg, 'query', '%v %o', 'liner-notes']) == profile['old_version'] + ' comms/liner-notes'
    public_directory_repairs = repair_legacy_sbin(Path('/'))
    account = pwd.getpwnam('liner_notes')
    web_account = pwd.getpwnam('liner_notes_web')
    assert account.pw_uid != 0 and web_account.pw_uid != 0 and account.pw_uid != web_account.pw_uid
    for directory, owner in (('/var/db/liner-notes', account), ('/var/db/liner-notes-web', web_account)):
        assert Path(directory).stat().st_uid == owner.pw_uid
    binary = '/usr/local/sbin/liner-notes'
    data = Path('/var/db/liner-notes')
    assert all(p.is_dir() and not p.is_symlink() for p in data.rglob('*'))
    PHASE = 'seed-data'
    (data / 'catalog.dat').write_text('1|0|all|member|Deployment acceptance\n')
    source = Server(binary, data / 'notes.dat', members=data / 'members.dat', catalog=data / 'catalog.dat',
                    options=['--database-structured', str(data / 'liner.sqlite')])
    client = Client(source.port)
    try:
        auth(client, 'deployprobe', True)
        post(client, 'retained article', 'package update article body')
        send(client, 'deployprobe', 'retained mail', 'package update mail body')
    finally:
        client.close()
        source.close()
    for item in [data, *data.rglob('*')]:
        os.chown(item, account.pw_uid, account.pw_gid)
    marker = data / 'private-key-fixture'
    marker.write_text('synthetic fixture, not a real identity\n')
    custom = Path('/usr/local/etc/liner-notes/cd-custom.conf')
    custom.write_text('# operator configuration retained\n')
    banner = Path('/usr/local/share/liner-notes/banner.txt')
    banner.write_text('Operator banner retained\n')
    before_hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (marker, custom, banner)}
    port = free_port()
    asset = Path('/usr/local/share/liner-notes')
    config = Path('/ci/service.conf')
    values = {'enable': 'YES', 'home': str(data), 'data_dir': str(data), 'program': binary,
              'run_user': 'liner_notes', 'bind': '127.0.0.1', 'port': str(port), 'language': 'en',
              'menu_dir': str(asset/'menus'), 'help_dir': str(asset/'help'), 'banner': '', 'sysinfo': '',
              'goodbye': '', 'login_text_dir': str(asset), 'database': str(data/'liner.sqlite'),
              'database_mode': 'structured', 'catalog': str(data/'catalog.dat'),
              'pidfile': '/var/run/liner-ci.pid', 'child_pidfile': '/var/run/liner-ci-child.pid',
              'log_dir': '/var/log/liner-notes', 'logfile': '/var/log/liner-notes/acceptance.log'}
    doors = Path('/ci/doors.conf')
    write_door_fixture(doors)
    values['doors'] = str(doors)
    config.write_text(''.join('liner_notes_' + k + '=' + shlex.quote(v) + '\n' for k, v in values.items()))
    rc = '/usr/local/etc/rc.d/liner_notes'
    rc_env = dict(ENV, LINER_NOTES_RC_CONF=str(config))
    running = False

    def service_check():
        nonlocal running
        command(['/bin/sh', rc, 'start'], env=rc_env)
        running = True
        command(['/bin/sh', rc, 'status'], env=rc_env)
        pid = Path('/var/run/liner-ci-child.pid').read_text().strip()
        assert int(command(['/bin/ps', '-o', 'uid=', '-p', pid])) == account.pw_uid
        def additional_checks(client):
            assert b'Back to BBS' in client.cmd('DOOR 1')
            web_check(port, web_account)
        try:
            readback(port, after=additional_checks)
        finally:
            command(['/bin/sh', rc, 'stop'], env=rc_env)
            running = False

    try:
        PHASE = 'old-services'
        service_check()
        PHASE = 'backup-restore'
        backup = Path('/ci/pre-update.tar.gz')
        command([sys.executable, ROOT/'scripts/maintenance.py', 'db-backup', '--root', data,
                 '--database', data/'liner.sqlite', '--board', 'notes.dat', '--members', 'members.dat', backup])
        restored = Path('/ci/restored')
        command(restore_command(backup, restored, '/usr/local/bin'))
        restore_tree = Path('/ci/restore-tree')
        restored_data = restore_tree/'var/db/liner-notes'
        restored_data.parent.mkdir(parents=True)
        shutil.copytree(restored, restored_data)
        login = [{'expect': 'RTBBS-PROBE '}, {'send': 'LOGIN deployprobe\n', 'expect': 'PASSWORD'},
                 {'send': PASSWORD+'\n', 'expect': 'LOGGED IN deployprobe'}]
        restore_profile = {'run_user': 'liner_notes', 'executable': binary, 'data_root': str(data),
                           'structured': True, 'database': str(data/'liner.sqlite'), 'board': str(data/'notes.dat'),
                           'members': str(data/'members.dat'), 'catalog': str(data/'catalog.dat'), 'probes': {
                               'login': {'exchanges': login},
                               'article': {'exchanges': login+[{'send': 'READ 1\n', 'expect': 'package update article body'}]},
                               'mail': {'exchanges': login+[{'send': 'MAIL READ 1\n', 'expect': 'package update mail body'}]}}}
        verify_application(restore_tree, restore_profile, trusted)
        PHASE = 'package-update'
        candidate = Path('/ci/packages')/manifest['packages']['liner-notes']['filename']
        metadata = json.loads(command(['/usr/bin/tar', '-xOf', candidate, '+MANIFEST']))
        assert metadata['name'] == 'liner-notes' and metadata['origin'] == 'comms/liner-notes'
        # A same-version development candidate still exercises a genuine pkg
        # revision upgrade. Its payload and maintainer scripts stay unchanged.
        revision_only = metadata['version'] == profile['old_version']
        repo = Path('/ci/repo')
        repo.mkdir()
        if revision_only:
            metadata['version'] += '_1'
            payload = Path('/ci/payload')
            payload.mkdir()
            command(['/usr/bin/tar', '-xf', candidate, '-C', payload, '--exclude', '+*'])
            manifest_path = Path('/ci/new-manifest.json')
            manifest_path.write_text(json.dumps(metadata))
            command([pkg, 'create', '-M', manifest_path, '-r', payload, '-o', repo])
        else:
            shutil.copy2(candidate, repo/candidate.name)
        package = next(repo.glob('*.pkg'))
        (repo/'repos').mkdir()
        (repo/'repos/release.conf').write_text('liner_notes_release: { url: "file:///ci/repo", enabled: yes, signature_type: "none" }\n')
        command([pkg, 'repo', repo])
        common = [pkg, '-o', 'REPOS_DIR=/ci/repo/repos', '-o', 'PKG_CACHEDIR=/ci/repo/cache']
        command([*common, 'update', '-f', '-r', 'liner_notes_release'])
        preview = command([*common, 'upgrade', '-n', '-U', '-r', 'liner_notes_release', 'liner-notes'])
        assert 'UPGRADED' in preview and 'INSTALLED' not in preview and 'REMOVED' not in preview
        for argv in package_commands('freebsd15', {'liner-notes': {'filename': package.name}}, repo):
            command(package_install_argv(argv))
            command(package_install_argv(argv))  # already-current package must not initialize data
        assert command([pkg, 'query', '%v %o', 'liner-notes']) == metadata['version']+' comms/liner-notes'
        assert {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (marker, custom, banner)} == before_hashes
        assert pwd.getpwnam('liner_notes').pw_uid == account.pw_uid
        assert data.stat().st_uid == account.pw_uid
        assert Path('/var/db/liner-notes-web').stat().st_uid == web_account.pw_uid
        PHASE = 'new-services'
        service_check()
        PHASE = 'link'
        link_service_check()
        # Real installed link service against two independent fixture BBSes.
        link_check = Path('/ci/link-check.py')
        link_check.write_text("import sys,asyncio,tempfile\nfrom pathlib import Path\nsys.path[:0]=['/ci/source/tests','/ci/source']\nimport test_link_service as t\nt.ROOT=Path('/usr/local/share/liner-notes')\nwith tempfile.TemporaryDirectory(prefix='jail-link-') as d: asyncio.run(t.exercise('/usr/local/sbin/liner-notes',Path(d)))\n")
        link_check.chmod(0o644)
        command([sys.executable, '-I', link_check], user=account, timeout=300)
        # Trusted main only, within this existing disposable VNET jail.
        command([sys.executable, ROOT/'tests/test_door_broker_linux.py', binary], timeout=180)
        # Planned recovery, not automatic rollback: restore the stopped DB copy
        # together with the old package, never run an old binary on an unknown
        # new DB schema. No pkg-delete/pkg-add pair is used.
        PHASE = 'old-recovery'
        shutil.move(data, '/ci/new-data-retained')
        shutil.copytree(restored, data)
        for item in [data, *data.rglob('*')]:
            os.chown(item, account.pw_uid, account.pw_gid)
        command(package_install_argv([pkg, 'add', '-f', '/ci/old.pkg']))
        assert command([pkg, 'query', '%v', 'liner-notes']) == profile['old_version']
        service_check()
        PHASE = 'final-inventory'
        after = command([pkg, 'query', '%n %v %o']).splitlines()
        assert sorted(x for x in before if not x.startswith('liner-notes ')) == sorted(x for x in after if not x.startswith('liner-notes '))
        Path('/ci/result.json').write_text(json.dumps({
            'source_sha': sha, 'full_service_update_verified': True, 'backup_restore_verified': True,
            'config_data_retained': True, 'unrelated_services_untouched': True,
            'old_package_restoration_verified': True, 'maintainer_scripts_executed': True,
            'account_ownership_verified': True, 'web_installed_service_verified': True,
            'link_installed_daemon_verified': True, 'link_rc_verified': True, 'door_pty_verified': True,
            'fixture_public_directory_repairs': public_directory_repairs,
            'revision_only_fixture': revision_only, 'previous_version': profile['old_version'],
            'candidate_version': metadata['version'], 'previous_source_sha': profile['old_release_source_sha']}, indent=2)+'\n')
    finally:
        if running:
            command(['/bin/sh', rc, 'stop'], env=rc_env)


def web_check(bbs_port, account):
    with tempfile.TemporaryDirectory(prefix='jail-web-') as temporary:
        base = Path(temporary)
        base.chmod(0o755)
        spool = base/'spool'
        spool.mkdir()
        os.chown(spool, account.pw_uid, account.pw_gid)
        port = free_port()
        config = base/'web.toml'
        config.write_text(f'''[web]
listen="127.0.0.1"
port={port}
origin="http://127.0.0.1:{port}"
secure_cookies=false
spool="{spool}"
messages="/usr/local/etc/liner-notes/web/messages"
templates="/usr/local/etc/liner-notes/web/templates"
[bbs]
host="127.0.0.1"
port={bbs_port}
telnet=true
''')
        config.chmod(0o644)
        conf = base/'rc.conf'
        conf.write_text('liner_notes_web_enable=YES\nliner_notes_web_user=liner_notes_web\n'
                        f'liner_notes_web_config={config}\nliner_notes_web_pidfile={base}/web.pid\nliner_notes_web_log={base}/web.log\n')
        rc_env = dict(ENV, LINER_NOTES_WEB_RC_CONF=str(conf))
        rc = '/usr/local/etc/rc.d/liner_notes_web'
        try:
            command(['/bin/sh', rc, 'start'], env=rc_env)
            command(['/bin/sh', rc, 'status'], env=rc_env)
            for attempt in range(50):
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{port}/notes/api/messages?lang=en', timeout=1) as response:
                        assert json.load(response)['name'] == 'Liner notes'
                    break
                except OSError:
                    if attempt == 49:
                        raise
                    time.sleep(.1)
        finally:
            command(['/bin/sh', rc, 'stop'], env=rc_env)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        # Root consumes only an allowlisted phase/type; private trace stays in jail.
        Path('/ci/failure.json').write_text(json.dumps({'phase': PHASE, 'type': type(error).__name__}))
        raise
