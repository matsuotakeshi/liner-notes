#!/usr/bin/env python3
"""Fresh GitHub admission plus ordered production transport; plan is default.

This workflow controller is not a root helper. It cannot authorize a deployment
using user-supplied JSON evidence; it fetches GitHub evidence itself each time.
"""
import argparse
import datetime
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from deploy_policy import (TARGETS, PLATFORMS, approval_phrase, authorization, canonical,
                           digest_file, require, sign, transaction_id,
                           validate_environment, validate_release, verify)
from deploy_transaction import rollout
from release_admission import latest_successful_run, verify_receipt

REPO = 'matsuotakeshi/liner-notes'
GATES = {
    'native': ('multi-os-ci.yml', 'push', 'Run native suites through existing CI SSH accounts', 'native-ci-results'),
    'linux': ('linux.yml', 'push', 'Build and test in the target distribution', None),
    'freebsd': ('freebsd-package.yml', 'push', 'Build and inspect pkg on FreeBSD CI host', 'liner-notes-freebsd15-packages'),
    'rocky': ('rocky-package.yml', 'push', 'Build 3 RPMs inside disposable Rocky Linux 10', 'liner-notes-rocky10-packages'),
    'debian': ('debian-package.yml', 'push', 'Build and test in isolated Debian 13', 'liner-notes-debian13-packages'),
    'release_set': ('release-set.yml', 'workflow_run', 'Save verified release-set receipt (no packages are deployed)', 'liner-notes-release-set-receipt'),
    'manual': ('manual.yml', 'push', None, 'canonical-manual'),
    'connectivity': ('production-ssh-smoke.yml', 'workflow_dispatch', 'Verify production SSH access', None),
    'deployment': ('deployment-safety.yml', 'push', 'Test closed deployment policy and isolated three-node transactions', None),
    'freebsd_upgrade': ('freebsd-upgrade-acceptance.yml', 'workflow_run', 'Verify full package upgrade in a disposable VNET jail', 'liner-notes-freebsd-upgrade'),
    'solaris': ('solaris-build-test.yml', 'push', 'Build and test exact main using the existing Solaris CI account', 'native-ci-solaris-results'),
    'selinux': ('selinux.yml', 'push', 'Verify the installed root-owned acceptance helper and run exact main', 'selinux-native-acceptance'),
    'doctor': ('doctor.yml', 'push', 'Read-only, malformed configuration, WAL and policy tests', 'doctor-evidence'),
}


def api(path, binary=False):
    require(path.startswith('repos/' + REPO + '/'), 'Unexpected GitHub API scope')
    result = subprocess.run(['gh', 'api', path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    require(result.returncode == 0, 'GitHub evidence unavailable')
    return result.stdout if binary else json.loads(result.stdout)


def artifact_json(artifact, basename, optional=False):
    data = api(f'repos/{REPO}/actions/artifacts/{artifact["id"]}/zip', binary=True)
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries = [i for i in archive.infolist() if Path(i.filename).name == basename]
        if optional and not entries:
            return None
        require(len(entries) == 1 and entries[0].file_size <= 8 * 1024 * 1024, 'Missing/ambiguous artifact evidence')
        return json.loads(archive.read(entries[0]))


def successful_execution(jobs, step, minimum=1):
    require(isinstance(jobs.get('jobs'), list) and jobs.get('total_count', 0) <= 100, 'Incomplete CI job evidence')
    if step is None:
        require(jobs['jobs'] and all(j.get('conclusion') == 'success' for j in jobs['jobs']), 'Required CI was skipped/failed')
        return
    executed = [j for j in jobs['jobs'] if any(s.get('name') == step and s.get('conclusion') == 'success' for s in j.get('steps', []))]
    require(len(executed) >= minimum and all(j.get('conclusion') == 'success' for j in executed), 'Required tests were not actually executed')


def fresh_connectivity(jobs, now=None):
    now = now or datetime.datetime.now(datetime.timezone.utc)
    steps = [s for j in jobs.get('jobs', []) if j.get('conclusion') == 'success'
             for s in j.get('steps', []) if s.get('name') == 'Verify production SSH access' and s.get('conclusion') == 'success']
    require(len(steps) == 1, 'Current production preflight step is missing or ambiguous')
    completed = datetime.datetime.fromisoformat(steps[0]['completed_at'].replace('Z', '+00:00'))
    require(completed.tzinfo is not None and 0 <= (now-completed).total_seconds() <= 3600,
            'Production connectivity check must have completed within the last hour')


def latest_full_run(document, sha, event, label):
    """Allow an explicit full manual run after a documentation-only push.

    Run/job/artifact provenance checks still apply; manual is not a substitute
    for executing the required build/test steps on this exact main revision.
    """
    events = {event, 'workflow_dispatch'}
    candidates = [r for r in document.get('workflow_runs', []) if r.get('head_sha') == sha
                  and r.get('head_branch') == 'main' and r.get('event') in events]
    require(candidates, 'No exact-main automatic/manual CI run')
    latest = max(candidates, key=lambda r: (r.get('run_number', 0), r.get('run_attempt', 0), r.get('id', 0)))
    return latest_successful_run(document, sha, latest['event'], label)


def trusted_release_ancestor(release_sha, controller_sha):
    """A later reviewed controller may deploy an older, still verified release."""
    require(api(f'repos/{REPO}/git/ref/heads/main')['object']['sha'] == controller_sha,
            'Main moved after controller dispatch; dispatch again and obtain fresh approval')
    if release_sha != controller_sha:
        comparison = api(f'repos/{REPO}/compare/{release_sha}...{controller_sha}')
        require(comparison.get('status') == 'ahead' and
                comparison.get('base_commit', {}).get('sha') == release_sha and
                comparison.get('merge_base_commit', {}).get('sha') == release_sha,
                'Release is not an ancestor of the reviewed main controller')


def select_artifact(document, name, run_id, sha):
    require(document.get('total_count', 0) <= 100, 'Incomplete artifact evidence')
    matches = [a for a in document.get('artifacts', []) if a.get('name') == name and a.get('expired') is False
               and a.get('workflow_run', {}).get('head_sha') == sha and a.get('workflow_run', {}).get('id') == run_id]
    require(len(matches) == 1, 'Required CI artifact is absent/expired')
    return matches[0]


def package_evidence(release, receipt, manifests, policy_manifest=None):
    """Bind optional independent-version policy to its own CI component."""
    sha = release['source_sha']
    require(receipt.get('source_sha') == sha and receipt.get('version') == release['version'], 'Release-set receipt differs')
    for platform in ('freebsd15', 'rocky10', 'debian13'):
        manifest = manifests[platform]
        require(manifest.get('source_sha') == sha and manifest.get('platform') == platform, 'Package provenance differs')
        for name, package in release['platforms'][platform].items():
            if name == 'liner-notes-selinux':
                continue
            expected = {k: v for k, v in package.items() if k != 'version'}
            require(manifest.get('version') == package['version'] and manifest['packages'].get(name) == expected,
                    'Signed package differs from CI-generated package')
            require(receipt['platforms'][platform]['version'] == package['version'] and
                    receipt['platforms'][platform]['packages'].get(name) ==
                    {'filename': package['filename'], 'sha256': package['sha256']}, 'Package is absent from verified release set')
    package = release['platforms']['rocky10'].get('liner-notes-selinux')
    if package is not None:
        component = receipt.get('components', {}).get('rocky10-selinux', {})
        expected = {k: v for k, v in package.items() if k != 'version'}
        require(policy_manifest is not None and policy_manifest.get('platform') == 'rocky10-selinux' and
                policy_manifest.get('source_sha') == sha and policy_manifest.get('version') == package['version'] and
                policy_manifest.get('packages') == {'liner-notes-selinux': expected},
                'Selected policy differs from exact-SHA policy CI')
        require(component.get('source_sha') == sha and component.get('version') == package['version'] and
                component.get('packages') == {'liner-notes-selinux': expected},
                'Selected policy is absent from verified release components')


def fresh_evidence(release):
    sha = release['source_sha']
    trusted_release_ancestor(sha, os.environ['GITHUB_SHA'])
    released = api(f'repos/{REPO}/releases/{release["release_id"]}')
    require(released.get('tag_name') == release['tag'] and released.get('draft') is False and released.get('prerelease') is False,
            'An approved published stable release is required')
    commit = api(f'repos/{REPO}/commits/{release["tag"]}')
    require(commit['sha'] == sha, 'Release tag points to another commit')
    evidence, artifacts = {}, {}
    for label, (workflow, event, step, artifact_name) in GATES.items():
        # Package/application evidence belongs to the immutable release. The
        # environmental preflight belongs to this newly reviewed controller.
        gate_sha = os.environ['GITHUB_SHA'] if label == 'connectivity' else sha
        runs = api(f'repos/{REPO}/actions/workflows/{workflow}/runs?head_sha={gate_sha}&per_page=100')
        run_id = latest_full_run(runs, gate_sha, event, label)
        jobs = api(f'repos/{REPO}/actions/runs/{run_id}/jobs?filter=latest&per_page=100')
        successful_execution(jobs, step, 2 if label == 'linux' else 1)
        if label == 'connectivity':
            fresh_connectivity(jobs)
        if label == 'deployment':
            successful_execution(jobs, 'Restore and boot the actual BBS using a disposable unprivileged account')
        if label == 'selinux':
            successful_execution(jobs, 'Compile the policy and its optional RPM in Rocky 10')
            successful_execution(jobs, 'Write exact-SHA Enforcing acceptance receipt')
        evidence[label] = run_id
        if artifact_name:
            document = api(f'repos/{REPO}/actions/runs/{run_id}/artifacts?per_page=100')
            artifacts[label] = select_artifact(document, artifact_name, run_id, sha)
            if label == 'selinux' and 'liner-notes-selinux' in release['platforms']['rocky10']:
                artifacts['selinux_policy'] = select_artifact(document, 'liner-notes-selinux-policy', run_id, sha)
            if label == 'release_set':
                verify_receipt(document, run_id, sha)
    native = artifact_json(artifacts['native'], 'results.json')
    require(native.get('ok') is True and native.get('source_sha') == sha and
            {r.get('target') for r in native.get('results', [])} == {'freebsd', 'rocky', 'ubuntu'} and
            all(r.get('source_sha') == sha and r.get('status') == 'passed' for r in native['results']), 'Native OS execution evidence is incomplete')
    solaris = artifact_json(artifacts['solaris'], 'results.json')
    require(solaris.get('ok') is True and solaris.get('source_sha') == sha and
            len(solaris.get('results', [])) == 1 and solaris['results'][0].get('target') == 'solaris' and
            solaris['results'][0].get('source_sha') == sha and solaris['results'][0].get('status') == 'passed',
            'Solaris native execution evidence is incomplete')
    selinux = artifact_json(artifacts['selinux'], 'SELINUX-ACCEPTANCE.json')
    require(selinux.get('source_sha') == sha and selinux.get('enforcing') is True and
            selinux.get('acceptance') == 'success', 'Dedicated-domain Enforcing acceptance evidence is missing')
    receipt = artifact_json(artifacts['release_set'], 'RELEASE-SET.json')
    manifests = {platform: artifact_json(artifacts[label], 'RELEASE-MANIFEST.json') for label, platform in
                 (('freebsd', 'freebsd15'), ('rocky', 'rocky10'), ('debian', 'debian13'))}
    policy_manifest = artifact_json(artifacts['selinux_policy'], 'RELEASE-MANIFEST.json') if 'selinux_policy' in artifacts else None
    package_evidence(release, receipt, manifests, policy_manifest)
    # A resolver fixture is deliberately not a substitute for the actual
    # package/scripts/services/restore acceptance required for srv.ysni.org.
    full = artifact_json(artifacts['freebsd_upgrade'], 'FREEBSD-SERVICE-UPGRADE.json')
    evidence['freebsd_service_upgrade_verified'] = bool(full and full.get('source_sha') == sha and
        full.get('candidate_package_sha256') == release['platforms']['freebsd15']['liner-notes']['sha256'] and
        full.get('candidate_version') == release['platforms']['freebsd15']['liner-notes']['version'] and
        full.get('revision_only_fixture') is False and all(
        full.get(field) is True for field in ('full_service_update_verified', 'backup_restore_verified',
                                              'config_data_retained', 'unrelated_services_untouched',
                                              'old_package_restoration_verified')))
    evidence['freebsd_previous_version'] = full.get('previous_version')
    return evidence, released


def transport(target, release_envelope, authorization_envelope, folder, key):
    release = release_envelope['document']
    identity = transaction_id(release)
    options = ['-i', str(key), '-o', 'IdentitiesOnly=yes', '-o', 'BatchMode=yes', '-o', 'PasswordAuthentication=no',
               '-o', 'KbdInteractiveAuthentication=no', '-o', 'StrictHostKeyChecking=yes',
               '-o', 'UserKnownHostsFile=' + str(ROOT / '.github/ssh/production_known_hosts')]
    host = 'ln-deploy@' + target
    # Staging is unprivileged; the root helper ignores all unlisted files and
    # copies verified packages into a private root-owned transaction directory.
    staging = '/var/tmp/liner-notes-deploy/' + identity
    subprocess.run(['ssh', '-T', *options, host, 'umask 077; mkdir -p ' + staging], check=True, stdout=subprocess.DEVNULL)
    for item in release['platforms'][PLATFORMS[target]].values():
        subprocess.run(['scp', *options, str(folder / item['filename']), host + ':' + staging + '/'], check=True, stdout=subprocess.DEVNULL)
    result = subprocess.run(['ssh', '-T', *options, host, 'sudo -n /usr/local/libexec/liner-notes-deploy-host'],
                            input=canonical({'release': release_envelope, 'authorization': authorization_envelope}),
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=7200)
    report = json.loads(result.stdout)
    require(report.get('target') == target and report.get('source_sha') == release['source_sha'], 'Host returned no matching deployment audit')
    require(result.returncode == 0 or report.get('result') != 'healthy', 'Host reported inconsistent success')
    return report


def record_target(target, release, invoke, results, save):
    try:
        result = invoke()
    except Exception:
        result = {'target': target, 'source_sha': release['source_sha'], 'result': 'transport-or-admission-failed',
                  'reason': 'inspect-private-host-state-before-resume',
                  'finished_at': datetime.datetime.now(datetime.timezone.utc).isoformat()}
    results.append(result)
    save()
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--release-id', type=int, required=True)
    parser.add_argument('--mode', choices=('plan', 'apply'), default='plan')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(json.dumps({'mode': args.mode, 'release_id': args.release_id,
                                      'controller_sha': os.environ.get('GITHUB_SHA'), 'targets': list(TARGETS),
                                      'run_id': os.environ.get('GITHUB_RUN_ID'),
                                      'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT'),
                                      'started_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                                      'result': 'checking-admission'}, indent=2) + '\n')
    try:
        execute(args)
    except Exception:
        report = json.loads(args.output.read_text())
        report.update(result='rejected-or-stopped', reason='admission-integrity-or-host-operation-failed',
                      finished_at=datetime.datetime.now(datetime.timezone.utc).isoformat())
        completed = {r.get('target') for r in report.get('results', [])}
        report['not_attempted_targets'] = [target for target in TARGETS if target not in completed]
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        raise


def execute(args):
    require(os.environ.get('GITHUB_REPOSITORY') == REPO and os.environ.get('GITHUB_REF') == 'refs/heads/main' and
            os.environ.get('GITHUB_EVENT_NAME') == 'workflow_dispatch', 'Trusted main dispatch required')
    run_id, attempt = int(os.environ['GITHUB_RUN_ID']), int(os.environ['GITHUB_RUN_ATTEMPT'])
    published = api(f'repos/{REPO}/releases/{args.release_id}')
    manifests = [a for a in published['assets'] if a['name'] == 'DEPLOYMENT-MANIFEST.json']
    require(len(manifests) == 1, 'Release lacks a signed deployment manifest')
    # gh release download handles redirect authentication without forwarding
    # the API token to a user-provided URL. Destination is an isolated directory.
    with tempfile.TemporaryDirectory(prefix='liner-deploy-') as temporary:
        folder = Path(temporary)
        subprocess.run(['gh', 'release', 'download', published['tag_name'], '--repo', REPO, '--pattern', 'DEPLOYMENT-MANIFEST.json', '--dir', str(folder)], check=True)
        envelope = json.loads((folder / 'DEPLOYMENT-MANIFEST.json').read_text())
        release = validate_release(verify(envelope, os.environ['LN_RELEASE_PUBLIC_KEY'].encode()))
        require(release['release_id'] == args.release_id, 'Release identity mismatch')
        initial = json.loads(args.output.read_text())
        initial.update(source_sha=release['source_sha'], release=release['tag'])
        args.output.write_text(json.dumps(initial, indent=2) + '\n')
        evidence, published = fresh_evidence(release)
        phrase = approval_phrase(release, run_id, attempt)
        if args.mode == 'plan':
            args.output.write_text(json.dumps({'mode': 'plan', 'source_sha': release['source_sha'], 'controller_sha': os.environ['GITHUB_SHA'], 'targets': list(TARGETS),
                                              'approval_comment': phrase, 'ci_runs': evidence}, indent=2) + '\n')
            print(phrase)
            return
        require(evidence['freebsd_service_upgrade_verified'], 'Full isolated FreeBSD package/service upgrade acceptance is missing; apply remains disabled')
        actor = validate_environment(api(f'repos/{REPO}/environments/production-release'),
                                     api(f'repos/{REPO}/environments/production-release/deployment-branch-policies'),
                                     api(f'repos/{REPO}/actions/runs/{run_id}/approvals'), release, run_id, attempt)
        auth = authorization(release, run_id, attempt, actor, freebsd_previous_version=evidence['freebsd_previous_version'])
        signed_auth = sign(auth, os.environ['LN_DEPLOY_AUTH_KEY'].encode())
        key = folder / 'ssh-key'
        key.write_text(os.environ['ID_ED25519_LN_DEPLOY'])
        key.chmod(0o600)
        for packages in release['platforms'].values():
            for item in packages.values():
                subprocess.run(['gh', 'release', 'download', release['tag'], '--repo', REPO, '--pattern', item['filename'], '--dir', str(folder)], check=True)
                path = folder / item['filename']
                require(path.stat().st_size == item['size'] and digest_file(path) == item['sha256'], 'Downloaded release package checksum mismatch')
        # Revalidate current-main and CI after approval and package download.
        fresh_evidence(release)
        results = []
        def save_audit():
            args.output.write_text(json.dumps({'release': release['tag'], 'source_sha': release['source_sha'], 'controller_sha': os.environ['GITHUB_SHA'], 'targets': list(TARGETS),
                                              'package_hashes': {p: {n: i['sha256'] for n, i in packages.items()} for p, packages in release['platforms'].items()},
                                              'approval': {'run_id': run_id, 'run_attempt': attempt, 'actor_id': actor},
                                              'ci_runs': evidence, 'results': results}, indent=2) + '\n')
        save_audit()
        try:
            def apply(target, doc, approval):
                return record_target(target, release,
                                     lambda: transport(target, envelope, signed_auth, folder, key), results, save_audit)
            rollout(release, auth, apply, dry_run=False)
        finally:
            key.unlink(missing_ok=True)


if __name__ == '__main__':
    try:
        main()
    except Exception:
        print('DEPLOYMENT REJECTED: admission, integrity or host operation failed; later targets were not updated', file=sys.stderr)
        raise SystemExit(1)
