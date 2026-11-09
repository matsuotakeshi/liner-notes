#!/usr/bin/env python3
"""Actual package payload/revision upgrade and rc lifecycle in a private root.

Runs unprivileged on the existing CI host. Package maintainer scripts/triggers
and account/ownership changes are DISABLED; the resulting evidence is partial.
Host service configuration is never loaded (LINER_NOTES_RC_CONF is mandatory).
"""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import pwd
import shlex
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'scripts'), str(ROOT / 'tests')]
from deploy_policy import package_commands
from test_board import Server, post
from test_runtime import Client as RawClient
from test_members import auth, PASSWORD
from test_mail import send
from test_telnet import Client as TelnetClient

assert sys.platform.startswith('freebsd') and os.geteuid() != 0
assert os.environ.get('LINER_FREEBSD_PACKAGE_CI') == '1'
work, package, stage, sha = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve(), sys.argv[4]
assert str(work).startswith(str(Path.home() / 'ci' / 'freebsd-pkg-'))
assert package.is_relative_to(work) and stage.is_relative_to(work)
PKG = '/usr/local/sbin/pkg'
env = dict(os.environ, INSTALL_AS_USER='yes', LC_ALL='C')


def command(argv, *, env=env):
    result = subprocess.run(list(map(str, argv)), env=env, capture_output=True, text=True, timeout=180)
    assert result.returncode == 0, (argv, result.stdout, result.stderr)
    return result.stdout.strip()


host_before = command([PKG, 'query', '%n %v %o'])
metadata = json.loads(command(['/usr/bin/tar', '-xOf', package, '+MANIFEST']))
old_version = metadata['version']
assert metadata['origin'] == 'comms/liner-notes' and metadata['name'] == 'liner-notes'
with tempfile.TemporaryDirectory(prefix='actual-upgrade-', dir=work) as temporary:
    base = Path(temporary)
    root, repo = base / 'root', base / 'repo'
    root.mkdir()
    repo.mkdir()
    dbdir = root / 'var/db/pkg'
    dbdir.mkdir(parents=True)
    # Dependency records are copied, never edited in the host package DB. Real
    # runtime execution uses already installed host shared libraries.
    with contextlib.closing(sqlite3.connect('file:/var/db/pkg/local.sqlite?mode=ro', uri=True)) as source:
        with contextlib.closing(sqlite3.connect(dbdir / 'local.sqlite')) as copy:
            source.backup(copy)
    common = [PKG, '-r', str(root), '-o', 'RUN_SCRIPTS=false', '-o', 'PKG_ENABLE_PLUGINS=false', '-o', 'PKG_TRIGGERS_ENABLE=false']
    command([*common, 'add', '-f', str(package)])
    binary = root / 'usr/local/sbin/liner-notes'
    rc = root / 'usr/local/etc/rc.d/liner_notes'
    assert binary.is_file() and rc.is_file()
    data = root / 'var/db/liner-notes'
    data.mkdir(parents=True, exist_ok=True)
    (data / 'catalog.dat').write_text('1|0|all|member|Package update test\n')
    source = Server(str(binary), data / 'notes.dat', members=data / 'members.dat', catalog=data / 'catalog.dat',
                    options=['--database-structured', str(data / 'liner.sqlite')])
    client = RawClient(source.port)
    try:
        auth(client, 'deployprobe', True)
        post(client, 'retained article', 'package update article body')
        send(client, 'deployprobe', 'retained mail', 'package update mail body')
    finally:
        client.close()
        source.close()
    custom = root / 'usr/local/etc/liner-notes/cd-custom.conf'
    custom.write_text('# retained custom setting\n')
    marker = data / 'private-key-fixture'
    marker.write_text('synthetic key fixture, never a production secret\n')
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    config = base / 'service.conf'
    asset = root / 'usr/local/share/liner-notes'
    settings = {
        'liner_notes_enable': 'YES', 'liner_notes_home': str(data), 'liner_notes_data_dir': str(data),
        'liner_notes_program': str(binary), 'liner_notes_menu_dir': str(asset / 'menus'),
        'liner_notes_help_dir': str(asset / 'help'), 'liner_notes_run_user': pwd.getpwuid(os.getuid()).pw_name,
        'liner_notes_bind': '127.0.0.1', 'liner_notes_port': str(port), 'liner_notes_language': 'en',
        'liner_notes_pidfile': str(base / 'supervisor.pid'), 'liner_notes_child_pidfile': str(base / 'child.pid'),
        'liner_notes_log_dir': str(base), 'liner_notes_logfile': str(base / 'daemon.log'),
        'liner_notes_database': str(data / 'liner.sqlite'), 'liner_notes_database_mode': 'structured',
        'liner_notes_catalog': str(data / 'catalog.dat'), 'liner_notes_banner': '',
        'liner_notes_sysinfo': '', 'liner_notes_goodbye': '', 'liner_notes_login_text_dir': str(asset),
    }
    config.write_text(''.join(k + '=' + shlex.quote(v) + '\n' for k, v in settings.items()))
    rc_env = dict(env, LINER_NOTES_RC_CONF=str(config))
    running = False
    def service_check():
        global running
        command(['/bin/sh', str(rc), 'start'], env=rc_env)
        running = True
        command(['/bin/sh', str(rc), 'status'], env=rc_env)
        c = TelnetClient(port)
        try:
            c.until()
            c.cmd('LOGIN deployprobe', b'Password (8-128 bytes)> ')
            assert b'Logged in as deployprobe' in c.cmd(PASSWORD)
            assert b'package update article body' in c.cmd('READ 1')
            c.cmd('0')
            assert b'package update mail body' in c.cmd('MAIL READ 1')
        finally:
            c.close()
            command(['/bin/sh', str(rc), 'stop'], env=rc_env)
            running = False
    try:
        service_check()
        # Reuse the shipped logical backup/restore implementation.
        backup = base / 'bbs-backup.tar.gz'
        admin = [sys.executable, ROOT / 'scripts/maintenance.py']
        command([*admin, 'db-backup', '--root', data, '--database', data / 'liner.sqlite',
                 '--board', 'notes.dat', '--members', 'members.dat', backup])
        restored = base / 'restored'
        command([*admin, 'db-restore', backup, restored])
        assert (restored / 'liner.sqlite').is_file()
        # Change only package revision; source and payload are the same exact
        # SHA. Maintainer scripts remain preserved in the archive but disabled.
        metadata['version'] = old_version + '_1'
        manifest = base / 'new-manifest.json'
        manifest.write_text(json.dumps(metadata))
        command([PKG, 'create', '-M', manifest, '-r', stage, '-o', repo])
        new_package = next(repo.glob('liner-notes-*.pkg'))
        (repo / 'repos').mkdir()
        (repo / 'repos/release.conf').write_text('liner_notes_release: { url: "file://' + str(repo) + '", enabled: yes, signature_type: "none" }\n')
        command([PKG, 'repo', repo])
        repo_args = ['-o', 'REPOS_DIR=' + str(repo / 'repos'), '-o', 'PKG_CACHEDIR=' + str(repo / 'cache')]
        command([*common, *repo_args, 'update', '-f', '-r', 'liner_notes_release'])
        for argv in package_commands('freebsd15', {'liner-notes': {'filename': new_package.name}}, repo):
            command([*common, *argv[1:]])
        assert command([*common, 'query', '%v %o', 'liner-notes']) == metadata['version'] + ' comms/liner-notes'
        assert custom.read_text() == '# retained custom setting\n'
        assert marker.read_text().startswith('synthetic key fixture')
        service_check()
        # Reinstall the exact escrowed old package into this private root only.
        # This is a recovery test; no delete/add pair or live data restore occurs.
        command([*common, 'add', '-f', package])
        assert command([*common, 'query', '%v', 'liner-notes']) == old_version
        service_check()
    finally:
        if running:
            command(['/bin/sh', str(rc), 'stop'], env=rc_env)
assert command([PKG, 'query', '%n %v %o']) == host_before
(work / 'artifacts/FREEBSD-ACTUAL-UPGRADE.json').write_text(json.dumps({
    'source_sha': sha, 'actual_package_payload_upgrade': True, 'isolated_rc_lifecycle': True,
    'login_article_mail_preserved': True, 'logical_backup_restore': True, 'old_package_recovery': True,
    'host_packages_unchanged': True, 'maintainer_scripts_executed': False, 'account_ownership_verified': False,
    'full_service_update_verified': False
}, indent=2) + '\n')
print('PASS actual FreeBSD package/rootdir rc update/recovery; maintainer/account tests remain separate')
