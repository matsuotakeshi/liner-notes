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
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = Path('/ci/source')
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'tests'), str(ROOT)]
from deploy_policy import package_commands
from deploy_restore import verify_application
from deploy_host import trusted
from test_board import Server, post
from test_runtime import Client
from test_members import auth, PASSWORD
from test_mail import send
from test_telnet import Client as TelnetClient

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


def main():
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
    account = pwd.getpwnam('liner_notes')
    web_account = pwd.getpwnam('liner_notes_web')
    assert account.pw_uid != 0 and web_account.pw_uid != 0 and account.pw_uid != web_account.pw_uid
    for directory, owner in (('/var/db/liner-notes', account), ('/var/db/liner-notes-web', web_account)):
        assert Path(directory).stat().st_uid == owner.pw_uid
    binary = '/usr/local/sbin/liner-notes'
    data = Path('/var/db/liner-notes')
    assert all(p.is_dir() and not p.is_symlink() for p in data.rglob('*'))
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
    doors.write_text('1|0|10|/usr/bin/true|/var/db/liner-notes|Fixture Door\n')
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
        client = TelnetClient(port)
        try:
            client.until()
            client.cmd('LOGIN deployprobe', b'Password (8-128 bytes)> ')
            assert b'Logged in as deployprobe' in client.cmd(PASSWORD)
            assert b'package update article body' in client.cmd('READ 1')
            client.cmd('0')
            assert b'package update mail body' in client.cmd('MAIL READ 1')
            client.cmd('0')
            assert b'Back to BBS' in client.cmd('DOOR 1')
            web_check(port, web_account)
        finally:
            client.close()
            command(['/bin/sh', rc, 'stop'], env=rc_env)
            running = False

    try:
        service_check()
        backup = Path('/ci/pre-update.tar.gz')
        command([sys.executable, ROOT/'scripts/maintenance.py', 'db-backup', '--root', data,
                 '--database', data/'liner.sqlite', '--board', 'notes.dat', '--members', 'members.dat', backup])
        restored = Path('/ci/restored')
        command([sys.executable, ROOT/'scripts/maintenance.py', 'db-restore', backup, restored])
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
            command(argv)
            command(argv)  # already-current package must not initialize data
        assert command([pkg, 'query', '%v %o', 'liner-notes']) == metadata['version']+' comms/liner-notes'
        assert {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (marker, custom, banner)} == before_hashes
        assert pwd.getpwnam('liner_notes').pw_uid == account.pw_uid
        assert data.stat().st_uid == account.pw_uid
        assert Path('/var/db/liner-notes-web').stat().st_uid == web_account.pw_uid
        service_check()
        # Real installed link service against two independent fixture BBSes.
        link_check = Path('/ci/link-check.py')
        link_check.write_text("import sys,asyncio,tempfile\nfrom pathlib import Path\nsys.path[:0]=['/ci/source/tests','/ci/source']\nimport test_link_service as t\nt.ROOT=Path('/usr/local/share/liner-notes')\nwith tempfile.TemporaryDirectory(prefix='jail-link-') as d: asyncio.run(t.exercise('/usr/local/sbin/liner-notes',Path(d)))\n")
        link_check.chmod(0o644)
        command([sys.executable, '-I', link_check], user=account, timeout=300)
        # Planned recovery, not automatic rollback: restore the stopped DB copy
        # together with the old package, never run an old binary on an unknown
        # new DB schema. No pkg-delete/pkg-add pair is used.
        shutil.move(data, '/ci/new-data-retained')
        shutil.copytree(restored, data)
        for item in [data, *data.rglob('*')]:
            os.chown(item, account.pw_uid, account.pw_gid)
        command([pkg, 'add', '-f', '/ci/old.pkg'])
        assert command([pkg, 'query', '%v', 'liner-notes']) == profile['old_version']
        service_check()
        after = command([pkg, 'query', '%n %v %o']).splitlines()
        assert sorted(x for x in before if not x.startswith('liner-notes ')) == sorted(x for x in after if not x.startswith('liner-notes '))
        Path('/ci/result.json').write_text(json.dumps({
            'source_sha': sha, 'full_service_update_verified': True, 'backup_restore_verified': True,
            'config_data_retained': True, 'unrelated_services_untouched': True,
            'old_package_restoration_verified': True, 'maintainer_scripts_executed': True,
            'account_ownership_verified': True, 'web_installed_service_verified': True,
            'link_installed_daemon_verified': True, 'door_pty_verified': True,
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
    main()
