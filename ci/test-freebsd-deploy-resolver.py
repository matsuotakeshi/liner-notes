#!/usr/bin/env python3
"""Real pkg upgrade resolver in an unprivileged rootdir; no host installation.

Synthetic packages have no scripts/dependencies. This proves the single-repo
upgrade path and unrelated-package/data retention, not full BBS service upgrade.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

PKG = '/usr/local/sbin/pkg'
assert sys.platform.startswith('freebsd') and os.geteuid() != 0
assert os.environ.get('LINER_FREEBSD_PACKAGE_CI') == '1'
work, sha = Path(sys.argv[1]).resolve(strict=True), sys.argv[2]
assert str(work).startswith(str(Path.home() / 'ci' / 'freebsd-pkg-'))
assert len(sha) == 40 and all(c in '0123456789abcdef' for c in sha)
environment = dict(os.environ, INSTALL_AS_USER='yes', LC_ALL='C')


def run(*args):
    process = subprocess.run([PKG, *map(str, args)], env=environment, capture_output=True, text=True, timeout=120)
    if process.returncode:
        print(process.stdout)
        print(process.stderr, file=sys.stderr)
        raise RuntimeError('Isolated pkg command failed')
    return process.stdout.strip()


host_before = run('query', '%n %v %o')
with tempfile.TemporaryDirectory(prefix='resolver-', dir=work) as temporary:
    base = Path(temporary)
    root, repo, configs = base / 'root', base / 'repo', base / 'repos'
    for directory in (root, repo, configs):
        directory.mkdir()
    abi = run('config', 'ABI')
    def package(name, version, out):
        payload = base / (name + version)
        path = payload / 'usr/local/share' / name / 'version'
        path.parent.mkdir(parents=True)
        path.write_text(version + '\n')
        manifest = {'name': name, 'version': version, 'origin': 'comms/' + name,
                    'comment': 'Isolated CD resolver fixture', 'desc': 'Disposable fixture, no services or dependencies',
                    'maintainer': 'ci@example.invalid', 'www': 'https://example.invalid/', 'prefix': '/usr/local',
                    'abi': abi, 'arch': abi, 'flatsize': path.stat().st_size,
                    'files': {'/usr/local/share/' + name + '/version': hashlib.sha256(path.read_bytes()).hexdigest()}}
        meta = base / (name + version + '.json')
        meta.write_text(json.dumps(manifest))
        out.mkdir(exist_ok=True)
        run('create', '-M', meta, '-r', payload, '-o', out)
        return next(out.glob(name + '-' + version + '.pkg'))
    old = package('liner-notes', '0.74.1', base / 'old')
    other = package('unrelated-fixture', '1.0', base / 'other')
    new = package('liner-notes', '0.75', repo)
    (configs / 'release.conf').write_text('liner_notes_release: { url: "file://' + str(repo) + '", enabled: yes, signature_type: "none" }\n')
    args = ['-r', str(root), '-o', 'REPOS_DIR=' + str(configs), '-o', 'RUN_SCRIPTS=false', '-o', 'PKG_ENABLE_PLUGINS=false', '-o', 'PKG_TRIGGERS_ENABLE=false']
    # Initial fixture installation only; updates below never delete/re-add.
    run(*args, 'add', old, other)
    data = root / 'var/db/liner-notes/test-data'
    data.parent.mkdir(parents=True)
    data.write_text('existing article/mail/file fixture\n')
    run('repo', repo)
    run(*args, 'update', '-f', '-r', 'liner_notes_release')
    plan = run(*args, 'upgrade', '-n', '-U', '-r', 'liner_notes_release', 'liner-notes')
    assert 'UPGRADED:' in plan and 'INSTALLED:' not in plan and 'REMOVED:' not in plan, plan
    run(*args, 'upgrade', '-y', '-U', '-r', 'liner_notes_release', 'liner-notes')
    assert run(*args, 'query', '%v %o', 'liner-notes') == '0.75 comms/liner-notes'
    assert run(*args, 'query', '%v', 'unrelated-fixture') == '1.0'
    assert data.read_text() == 'existing article/mail/file fixture\n'
    assert (root / 'usr/local/share/liner-notes/version').read_text() == '0.75\n'
    run(*args, 'upgrade', '-y', '-U', '-r', 'liner_notes_release', 'liner-notes')
    assert data.read_text() == 'existing article/mail/file fixture\n'
assert run('query', '%n %v %o') == host_before, 'Host package database changed'
(work / 'artifacts/FREEBSD-DEPLOY-RESOLVER.json').write_text(json.dumps({
    'source_sha': sha, 'resolver_upgrade': True, 'repeat_no_change': True,
    'host_packages_unchanged': True, 'full_service_update_verified': False,
    'scope': 'unprivileged rootdir synthetic packages; scripts disabled'
}, indent=2) + '\n')
print('PASS isolated real pkg resolver upgrade; full BBS package/service upgrade is a separate gate')
