#!/usr/local/bin/python3.12 -I
"""Installed root entrypoint for a disposable VNET jail on 10.0.0.50 only.

Only an exact main SHA is accepted. A read-only Actions token arrives on stdin;
source/package paths and commands are never accepted from the SSH account.
Template preparation and this entrypoint's installation require separate setup.
"""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

REPO = 'matsuotakeshi/liner-notes'
BASE = Path('/var/db/liner-notes-ci/freebsd-upgrade')
TEMPLATE = Path('/var/db/liner-notes-ci/freebsd-template')
SOURCE_HELPER = Path('/usr/local/libexec/liner-notes-ci/selinux_root.py')
ENV = {'PATH': '/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin',
       'HOME': '/root', 'LC_ALL': 'C', 'PYTHONNOUSERSITE': '1'}
DIAGNOSTIC = None
PHASE = "admission"
JAIL_FAILURE = None
PHASES = {"admission", "source", "artifact", "clone", "jail-create", "jail-execute", "cleanup", "host-inventory"}
JAIL_PHASES = {"admission", "seed-data", "old-services", "backup-restore", "package-update", "new-services", "link", "old-recovery", "final-inventory"}


def public_failure(error, phase, child=None):
    known = {"ValueError", "AssertionError", "OSError", "FileNotFoundError", "PermissionError", "TimeoutExpired", "CalledProcessError", "HTTPError", "URLError", "JSONDecodeError"}
    kind = type(error).__name__
    code = error.returncode if isinstance(error, subprocess.CalledProcessError) else None
    reason = 'acceptance-rejected'
    if isinstance(error, subprocess.TimeoutExpired):
        reason = 'timeout'
    elif isinstance(error, subprocess.CalledProcessError):
        reason = 'command-failed'
    elif isinstance(error, FileNotFoundError):
        reason = 'missing-prerequisite'
    elif isinstance(error, PermissionError):
        reason = 'permission-denied'
    elif type(error) is ValueError:
        # Only exact literals from this reviewed helper can become public codes.
        reason = {'Missing installed prerequisite': 'missing-prerequisite',
                  'Main advanced before jail execution': 'main-advanced',
                  'VNET kernel required': 'vnet-unavailable',
                  'Actions archive digest absent or mismatched': 'artifact-integrity',
                  'Same-SHA package artifact required': 'package-evidence-missing',
                  'Latest package attempt did not succeed': 'package-ci-not-successful',
                  'Insufficient space for isolated jail and recovery copies': 'insufficient-space'}.get(str(error), reason)
    result = {"result": "failed", "phase": phase if isinstance(phase, str) and phase in PHASES else "admission",
              "type": kind if kind in known else "Error",
              "exit_code": code if type(code) is int and -255 <= code <= 255 else None,
              "reason": reason}
    if isinstance(child, dict) and isinstance(child.get("phase"), str) and child["phase"] in JAIL_PHASES:
        result["jail_phase"] = child["phase"]
        result["jail_type"] = child.get("type") if isinstance(child.get("type"), str) and child["type"] in known else "Error"
    return result


def require(condition, message):
    if not condition:
        raise ValueError(message)


def trusted(path, directory=False):
    for item in [path, *path.parents]:
        info = item.lstat()
        require(not stat.S_ISLNK(info.st_mode) and info.st_uid == 0 and not info.st_mode & 0o022,
                'Root-owned, non-writable helper/template ancestry required')
    require(path.is_dir() if directory else path.is_file(), 'Missing installed prerequisite')


def diagnostic(record):
    if DIAGNOSTIC is None:
        return
    fd = os.open(DIAGNOSTIC, os.O_WRONLY | os.O_CREAT | os.O_APPEND | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'a', encoding='utf8') as stream:
        info = os.fstat(stream.fileno())
        require(stat.S_ISREG(info.st_mode) and info.st_uid == 0 and not info.st_mode & 0o077,
                'Unsafe private diagnostic log')
        stream.write(json.dumps(record) + '\n')
        stream.flush()
        os.fsync(stream.fileno())


def command(*args, timeout=180):
    try:
        result = subprocess.run(list(map(str, args)), env=ENV, text=True, capture_output=True,
                                timeout=timeout, check=False)
    except subprocess.TimeoutExpired as error:
        diagnostic({'program': str(args[0]), 'result': 'timeout',
                    'stdout': str(error.stdout or '')[-65536:], 'stderr': str(error.stderr or '')[-65536:]})
        raise
    except OSError as error:
        diagnostic({'program': str(args[0]), 'result': 'start-failed', 'error': str(error)})
        raise
    diagnostic({'program': str(args[0]), 'returncode': result.returncode,
                'stdout': result.stdout[-65536:], 'stderr': result.stderr[-65536:]})
    result.check_returncode()
    return result.stdout.strip()


class SafeRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, url):
        require(url.startswith('https://'), 'Refuse non-HTTPS artifact redirect')
        result = super().redirect_request(request, fp, code, message, headers, url)
        if result:
            result.remove_header('Authorization')
        return result


def fetch(path, token, limit=8 * 1024 * 1024, binary=False):
    require(path.startswith('repos/' + REPO + '/'), 'Unexpected API scope')
    request = urllib.request.Request('https://api.github.com/' + path, headers={
        'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
        'User-Agent': 'liner-notes-isolated-upgrade'})
    with urllib.request.build_opener(SafeRedirect()).open(request, timeout=90) as response:
        content = response.read(limit + 1)
    require(len(content) <= limit, 'Artifact response too large')
    return content if binary else json.loads(content)


def select_package_run(document, sha):
    runs = [r for r in document.get('workflow_runs', []) if r.get('head_sha') == sha
            and r.get('head_branch') == 'main' and r.get('event') in ('push', 'workflow_dispatch')]
    require(runs, 'No trusted main package build')
    run = max(runs, key=lambda r: (r.get('run_number', 0), r.get('run_attempt', 0), r['id']))
    require(run.get('status') == 'completed' and run.get('conclusion') == 'success',
            'Latest package attempt did not succeed')
    return run


def validate_jail_paths(root):
    require(not (root / 'ci').exists() and not (root / 'ci').is_symlink(), 'Template has unexpected CI payload')
    require((root / 'dev').is_dir() and not (root / 'dev').is_symlink()
            and (root / 'dev').resolve() == root.resolve() / 'dev', 'Unsafe jail devfs mountpoint')


def make_jail_root_searchable(root):
    """Only the new jail's filesystem root is public, never its host ancestors."""
    require(root.is_dir() and not root.is_symlink(), 'Unsafe jail filesystem root')
    root.chmod(0o755)


def write_door_fixture(path):
    """Publish this synthetic config to the unprivileged fixture runtime."""
    with path.open('x', encoding='utf8') as stream:
        stream.write('1|0|10|/usr/bin/true|/var/db/liner-notes|Fixture Door\n')
    path.chmod(0o644)


def unpack_package(content, artifact, sha, destination):
    require(artifact.get('digest') == 'sha256:' + hashlib.sha256(content).hexdigest(),
            'Actions archive digest absent or mismatched')
    with zipfile.ZipFile(io.BytesIO(content)) as archive:
        seen = set()
        total = 0
        for item in archive.infolist():
            path = PurePosixPath(item.filename)
            mode = item.external_attr >> 16
            require(not path.is_absolute() and '..' not in path.parts and '\\' not in item.orig_filename
                    and item.filename not in seen and not stat.S_ISLNK(mode), 'Unsafe artifact member')
            seen.add(item.filename)
            total += item.file_size
            require(total <= 1024 ** 3 and len(seen) <= 100, 'Artifact budget exceeded')
        manifests = [i for i in archive.infolist() if PurePosixPath(i.filename).name == 'RELEASE-MANIFEST.json']
        require(len(manifests) == 1 and manifests[0].file_size < 65536, 'Ambiguous package manifest')
        manifest = json.loads(archive.read(manifests[0]))
        require(manifest.get('source_sha') == sha and manifest.get('platform') == 'freebsd15'
                and set(manifest.get('packages', {})) == {'liner-notes'}, 'Package provenance differs')
        package = manifest['packages']['liner-notes']
        filename = package.get('filename', '')
        require(re.fullmatch(r'liner-notes-[0-9][A-Za-z0-9.+_~-]*\.pkg', filename), 'Unexpected package name')
        matches = [i for i in archive.infolist() if PurePosixPath(i.filename).name == filename]
        require(len(matches) == 1 and matches[0].file_size == package.get('size'), 'Package size differs')
        payload = archive.read(matches[0])
        require(hashlib.sha256(payload).hexdigest() == package.get('sha256'), 'Package hash differs')
        (destination / filename).write_bytes(payload)
        (destination / 'RELEASE-MANIFEST.json').write_text(json.dumps(manifest), encoding='utf8')
        return package


def main():
    global DIAGNOSTIC, PHASE, JAIL_FAILURE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sha')
    args = parser.parse_args()
    require(re.fullmatch(r'[0-9a-f]{40}', args.sha), 'Expected exact commit SHA')
    require(os.geteuid() == 0 and sys.platform.startswith('freebsd'), 'FreeBSD root helper required')
    os.environ.clear()
    os.environ.update(ENV)
    os.umask(0o077)
    require(re.search(r'\binet 10\.0\.0\.50\b', command('/sbin/ifconfig', '-a')), 'Designated test VM only')
    require(command('/usr/bin/uname', '-m') == 'amd64' and command('/usr/bin/uname', '-r').startswith('15.'), 'FreeBSD 15 amd64 required')
    require(command('/sbin/sysctl', '-n', 'kern.features.vimage') == '1', 'VNET kernel required')
    trusted(BASE.parent, True)
    BASE.mkdir(mode=0o700, exist_ok=True)
    trusted(BASE, True)
    require(not BASE.stat().st_mode & 0o077, 'Private CI work directory required')
    DIAGNOSTIC = BASE / 'helper.log'
    trusted(Path(__file__).resolve())
    trusted(SOURCE_HELPER)
    spec = importlib.util.spec_from_file_location('installed_trusted_source', SOURCE_HELPER)
    source_api = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(source_api)
    require(source_api.current_main() == args.sha, 'Only current reviewed main is permitted')
    request = json.loads(sys.stdin.buffer.read(16385))
    require(set(request) == {'token'} and isinstance(request['token'], str)
            and 20 <= len(request['token']) <= 4096 and '\n' not in request['token'], 'Read-only Actions token required')
    token = request['token']
    trusted(TEMPLATE, True)
    require(not TEMPLATE.stat().st_mode & 0o077, 'Template must be root-private mode 0700')
    trusted(TEMPLATE / 'root', True)
    trusted(TEMPLATE / 'template.json')
    profile = json.loads((TEMPLATE / 'template.json').read_text())
    require(set(profile) == {'old_version', 'old_package_sha256', 'old_release_source_sha'}, 'Unexpected template profile')
    require(re.fullmatch(r'[0-9a-f]{40}', profile['old_release_source_sha']), 'Template needs stable release provenance')
    trusted(TEMPLATE / 'old.pkg')
    require(hashlib.sha256((TEMPLATE / 'old.pkg').read_bytes()).hexdigest() == profile['old_package_sha256'], 'Old package escrow hash differs')
    require(all(p.is_dir() and not p.is_symlink() for p in (TEMPLATE / 'root/var/db/liner-notes').rglob('*')),
            'Template must contain only empty BBS/spool directory trees')
    template_root = (TEMPLATE / 'root').resolve()
    for row in command('/sbin/mount', '-p').splitlines():
        fields = row.split()
        require(len(fields) >= 2, 'Cannot inspect mount inventory')
        mountpoint = Path(fields[1])
        require(mountpoint != template_root and not mountpoint.is_relative_to(template_root),
                'Template contains a mounted filesystem; unmount it before acceptance')
    template_size = sum(p.lstat().st_size for p in template_root.rglob('*') if stat.S_ISREG(p.lstat().st_mode))
    require(shutil.disk_usage(BASE).free >= 3 * template_size + 2 * 1024 ** 3,
            'Insufficient space for isolated jail and recovery copies')
    import fcntl
    with (BASE / 'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        work = Path(tempfile.mkdtemp(prefix=args.sha[:12] + '-', dir=BASE))
        DIAGNOSTIC = work / 'helper.log'
        source, packages = work / 'source', work / 'packages'
        source.mkdir(mode=0o755)
        packages.mkdir(mode=0o755)
        PHASE = 'source'
        archive = source_api.get('https://codeload.github.com/' + REPO + '/tar.gz/' + args.sha, 128 * 1024 ** 2)
        source_api.extract(archive, source, args.sha)
        PHASE = 'artifact'
        run = select_package_run(fetch(f'repos/{REPO}/actions/workflows/freebsd-package.yml/runs?head_sha={args.sha}&per_page=100', token), args.sha)
        jobs = fetch(f'repos/{REPO}/actions/runs/{run["id"]}/jobs?filter=latest&per_page=100', token)
        require(jobs.get('total_count', 0) <= 100 and any(j.get('conclusion') == 'success' and any(
            s.get('name') == 'Build and inspect pkg on FreeBSD CI host' and s.get('conclusion') == 'success'
            for s in j.get('steps', [])) for j in jobs.get('jobs', [])), 'Real package build did not execute')
        listing = fetch(f'repos/{REPO}/actions/runs/{run["id"]}/artifacts?per_page=100', token)
        candidates = [a for a in listing.get('artifacts', []) if a.get('name') == 'liner-notes-freebsd15-packages'
                      and a.get('expired') is False and a.get('workflow_run', {}).get('id') == run['id']
                      and a.get('workflow_run', {}).get('head_sha') == args.sha]
        require(len(candidates) == 1, 'Same-SHA package artifact required')
        artifact = candidates[0]
        package = unpack_package(fetch(f'repos/{REPO}/actions/artifacts/{artifact["id"]}/zip', token, 1024 ** 3, True), artifact, args.sha, packages)
        del request, token
        PHASE = 'clone'
        root = work / 'jail'
        root.mkdir(mode=0o755)
        template_tar = work / 'template.tar'
        command('/usr/bin/tar', '-cf', template_tar, '-C', TEMPLATE / 'root', '.', timeout=600)
        command('/usr/bin/tar', '-xpf', template_tar, '-C', root, timeout=600)
        validate_jail_paths(root)
        make_jail_root_searchable(root)
        shutil.copytree(source, root / 'ci/source', symlinks=False)
        shutil.copytree(packages, root / 'ci/packages')
        shutil.copyfile(TEMPLATE / 'old.pkg', root / 'ci/old.pkg')
        (root / 'ci/template.json').write_text(json.dumps(profile))
        command('/bin/chmod', '-R', 'a+rX', root / 'ci')
        host_packages = command('/usr/local/sbin/pkg', 'query', '%n %v %o')
        require(source_api.current_main() == args.sha, 'Main advanced before jail execution')
        jail = 'liner-ci-' + work.name
        mounted, created = False, False
        try:
            PHASE = 'jail-create'
            command('/sbin/mount', '-t', 'devfs', '-o', 'ruleset=4', 'devfs', root / 'dev')
            mounted = True
            command('/usr/sbin/jail', '-c', 'name=' + jail, 'path=' + str(root), 'host.hostname=liner-ci.invalid',
                    'vnet', 'persist', 'allow.raw_sockets=0', 'allow.mount=0', 'enforce_statfs=2')
            created = True
            command('/usr/sbin/jexec', jail, '/sbin/ifconfig', 'lo0', 'inet', '127.0.0.1', 'up')
            PHASE = 'jail-execute'
            try:
                output = command('/usr/sbin/jexec', jail, '/usr/local/bin/python3.12', '-I',
                                 '/ci/source/ci/freebsd_upgrade_jail.py', args.sha, timeout=1500)
            except Exception:
                try:
                    failure_path = root / 'ci/failure.json'
                    if failure_path.is_file() and not failure_path.is_symlink() and failure_path.stat().st_size <= 1024:
                        JAIL_FAILURE = json.loads(failure_path.read_text())
                except (OSError, ValueError):
                    pass
                raise
            (work / 'acceptance.log').write_text(output)
            report = json.loads((root / 'ci/result.json').read_text())
            require(report.get('source_sha') == args.sha, 'Jail receipt source mismatch')
            report.update(package_artifact_id=artifact['id'], package_workflow_run_id=run['id'],
                          candidate_package_sha256=package['sha256'], source_archive_sha256=hashlib.sha256(archive).hexdigest())
        finally:
            try:
                if created:
                    command('/usr/sbin/jail', '-r', jail)
                if mounted:
                    command('/sbin/umount', root / 'dev')
            except Exception:
                PHASE = 'cleanup'
                raise
            # Keep the private fixture for diagnosis; never recursively delete a
            # tree that may still have a jail or mounted devfs after a failure.
        PHASE = 'host-inventory'
        require(command('/usr/local/sbin/pkg', 'query', '%n %v %o') == host_packages, 'Host package inventory changed')
        report['host_packages_unchanged'] = True
        (work / 'FREEBSD-SERVICE-UPGRADE.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        try:
            diagnostic({'result': 'failed', 'type': type(error).__name__, 'detail': str(error)})
        except Exception:
            pass
        print(json.dumps(public_failure(error, PHASE, JAIL_FAILURE)), file=sys.stderr)
        raise SystemExit(1)
