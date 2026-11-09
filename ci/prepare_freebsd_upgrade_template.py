#!/usr/local/bin/python3.12 -I
"""Explicit, one-time TEST VM setup after main review and separate approval.

No host packages/services are changed. Internet access exists only for installing
dependencies into a new bootstrap jail. Acceptance uses an offline VNET clone.
Never run this from a checkout writable by the CI account.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile

SOURCE = Path(__file__).resolve().parents[1]
for candidate in (SOURCE/'ci/freebsd_upgrade_root.py', SOURCE/'ci/selinux_root.py', Path(__file__).resolve()):
    for path in (candidate, *candidate.parents):
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
            raise SystemExit('One-time root setup must use root-owned, non-writable reviewed source')
sys.path.insert(0, str(SOURCE/'ci'))
import freebsd_upgrade_root as shared

BASE_URL = 'https://download.freebsd.org/releases/amd64/amd64/15.1-RELEASE/base.txz'
BASE_SHA256 = '3768988b151c20f965679062b065c63a977d6bbb9f47fd83695ec2c40790c18f'
OLD_URL = 'https://github.com/matsuotakeshi/liner-notes/releases/download/v0.74.1/liner-notes-0.74.1.pkg'
OLD_SHA256 = 'b38be5af01a943003c78fabe8e95bc5f860432af7ee222df1d81ea1ed737c6cb'
OLD_SOURCE = 'f9983e14dc70034d500677f1163b73830440551a'


def main():
    require, command = shared.require, shared.command
    require(len(sys.argv) == 2 and re.fullmatch(r'[0-9a-f]{40}', sys.argv[1]), 'Exact reviewed main SHA required')
    sha = sys.argv[1]
    require(os.geteuid() == 0 and sys.platform.startswith('freebsd'), 'FreeBSD root setup required')
    os.environ.clear()
    os.environ.update(shared.ENV)
    os.umask(0o077)
    require(re.search(r'\binet 10\.0\.0\.50\b', command('/sbin/ifconfig', '-a')), 'Designated test VM only')
    require(command('/usr/bin/uname', '-r').startswith('15.1-') and command('/usr/bin/uname', '-m') == 'amd64',
            'This pinned template is FreeBSD 15.1 amd64 only')
    for name in ('prepare_freebsd_upgrade_template.py', 'freebsd_upgrade_root.py', 'selinux_root.py'):
        shared.trusted(SOURCE/'ci'/name)
    spec = importlib.util.spec_from_file_location('trusted_source', SOURCE/'ci/selinux_root.py')
    source_api = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(source_api)
    require(source_api.current_main() == sha, 'Main advanced; review current source before setup')
    # Bind every privileged script read from this root-owned checkout to the
    # independently downloaded main tree; a CLI SHA by itself is insufficient.
    parent = shared.TEMPLATE.parent
    shared.trusted(parent.parent, True)
    parent.mkdir(mode=0o700, exist_ok=True)
    shared.trusted(parent, True)
    require(not parent.stat().st_mode & 0o077, 'Private template parent required')
    require(not shared.TEMPLATE.exists() and not shared.TEMPLATE.is_symlink(), 'Never overwrite an existing template')
    work = Path(tempfile.mkdtemp(prefix='bootstrap-'+sha[:12]+'-', dir=parent))
    shared.DIAGNOSTIC = work/'setup.log'
    reviewed = work/'reviewed'
    reviewed.mkdir()
    source_api.extract(source_api.get('https://codeload.github.com/'+shared.REPO+'/tar.gz/'+sha, 128*1024**2), reviewed, sha)
    for name in ('prepare_freebsd_upgrade_template.py', 'freebsd_upgrade_root.py', 'selinux_root.py'):
        require((reviewed/'ci'/name).read_bytes() == (SOURCE/'ci'/name).read_bytes(), 'Installed setup source differs from reviewed main')
    require(shutil.disk_usage(parent).free > 8*1024**3, 'At least 8 GiB free required before bootstrap')
    for name, url, expected, limit in [('base.txz', BASE_URL, BASE_SHA256, 512*1024**2),
                                       ('old.pkg', OLD_URL, OLD_SHA256, 64*1024**2)]:
        content = source_api.get(url, limit)
        require(hashlib.sha256(content).hexdigest() == expected, 'Pinned official download hash differs')
        (work/name).write_bytes(content)
    candidate = work/'template'
    candidate.mkdir(mode=0o700)
    root = candidate/'root'
    root.mkdir(mode=0o755)
    command('/usr/bin/tar', '-xpf', work/'base.txz', '-C', root, timeout=300)
    shared.validate_jail_paths(root)
    (root/'ci').mkdir(mode=0o755)
    shutil.copyfile(work/'old.pkg', root/'ci/old.pkg')
    shutil.copyfile('/etc/resolv.conf', root/'etc/resolv.conf')
    (root/'etc/rc.conf').write_text('sshd_enable="NO"\nliner_notes_enable="NO"\nliner_notes_web_enable="NO"\n')
    jail = 'liner-bootstrap-'+sha[:12]
    mounted, created = False, False
    host_packages = command('/usr/local/sbin/pkg', 'query', '%n %v %o')
    try:
        command('/sbin/mount', '-t', 'devfs', '-o', 'ruleset=4', 'devfs', root/'dev')
        mounted = True
        command('/usr/sbin/jail', '-c', 'name='+jail, 'path='+str(root), 'host.hostname=liner-bootstrap.invalid',
                'ip4.addr=10.0.0.50', 'ip6=disable', 'persist', 'allow.raw_sockets=0', 'allow.mount=0', 'enforce_statfs=2')
        created = True
        command('/usr/sbin/jexec', jail, '/usr/bin/env', 'ASSUME_ALWAYS_YES=yes', '/usr/sbin/pkg', 'bootstrap', '-y', timeout=300)
        metadata = json.loads(command('/usr/bin/tar', '-xOf', work/'old.pkg', '+MANIFEST'))
        require(metadata['name'] == 'liner-notes' and metadata['version'] == '0.74.1'
                and metadata['origin'] == 'comms/liner-notes', 'Pinned package identity differs')
        dependencies = sorted(set(metadata.get('deps', {})) | {'python312', 'py312-aiohttp', 'py312-cryptography', 'sqlite3', 'lrzsz'})
        require(all(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_+.-]*', n) for n in dependencies), 'Unexpected dependency name')
        command('/usr/sbin/jexec', jail, '/usr/local/sbin/pkg', 'install', '-y', *dependencies, timeout=900)
        command('/usr/sbin/jexec', jail, '/usr/local/sbin/pkg', 'add', '/ci/old.pkg', timeout=300)
        inventory = command('/usr/sbin/jexec', jail, '/usr/local/sbin/pkg', 'query', '%n %v %o')
        require(all(p.is_dir() and not p.is_symlink() for p in (root/'var/db/liner-notes').rglob('*')),
                'Initial installation unexpectedly created BBS data; retain and inspect')
        result = command('/usr/sbin/jexec', jail, '/bin/ps', '-ax', '-o', 'command=')
        require('/usr/local/sbin/liner-notes ' not in result and ' -m webui ' not in result,
                'Template unexpectedly started a BBS service')
    finally:
        if created:
            command('/usr/sbin/jail', '-r', jail)
        if mounted:
            command('/sbin/umount', root/'dev')
    require(command('/usr/local/sbin/pkg', 'query', '%n %v %o') == host_packages, 'Host package inventory changed')
    # Delete only the fixed synthetic staging directory after jail/devfs cleanup;
    # it contains solely the verified old package copied above.
    require((root/'ci').resolve() == root.resolve()/'ci' and not (root/'ci').is_symlink(), 'Unsafe staging cleanup path')
    (root/'ci/old.pkg').unlink()
    (root/'ci').rmdir()
    (root/'etc/resolv.conf').write_text('# Acceptance clones use offline VNET loopback only.\n')
    shutil.copyfile(work/'old.pkg', candidate/'old.pkg')
    (candidate/'template.json').write_text(json.dumps({'old_version': '0.74.1', 'old_package_sha256': OLD_SHA256,
                                                       'old_release_source_sha': OLD_SOURCE}, indent=2)+'\n')
    (work/'SETUP.json').write_text(json.dumps({'source_sha': sha, 'old_source_sha': OLD_SOURCE,
                                             'old_package_sha256': OLD_SHA256, 'base_sha256': BASE_SHA256,
                                             'host_packages_unchanged': True, 'template_inventory': inventory.splitlines()}, indent=2)+'\n')
    candidate.rename(shared.TEMPLATE)
    print('Prepared root-private FreeBSD template; host packages/services unchanged. Acceptance has not run.')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        shared.diagnostic({'result': 'bootstrap-failed', 'type': type(error).__name__, 'detail': str(error)})
        print('Template bootstrap failed; private diagnostic retained; host services were not operated.', file=sys.stderr)
        raise SystemExit(1)
