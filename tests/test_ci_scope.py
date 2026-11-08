#!/usr/bin/env python3
"""Real Git histories and workflow contracts for documentation-only CI."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'ci'))
from change_scope import classify, documentation


class HistoryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.repo = Path(self.tmp.name)
        self.git('init', '-q')
        self.git('config', 'user.name', 'CI scope test')
        self.git('config', 'user.email', 'ci-scope@example.invalid')
        self.git('config', 'core.autocrlf', 'false')
        self.write('docs/guide.md', 'guide\n')
        self.write('src/test.c', 'code\n')
        self.base = self.commit()

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.repo), *args], stderr=subprocess.PIPE).decode().strip()

    def write(self, path, text):
        p = self.repo / path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text, encoding='utf-8')

    def commit(self):
        self.git('add', '-A')
        self.git('-c', 'commit.gpgsign=false', 'commit', '-qm', 'scope fixture')
        return self.git('rev-parse', 'HEAD')

    def push(self, base=None, head=None, ref='refs/heads/main'):
        return classify(self.repo, 'push', {'before': base or self.base, 'after': head or self.git('rev-parse', 'HEAD')}, ref)

    def test_documentation_and_help_only(self):
        self.write('README.md', 'intro')
        self.write('help/ja/read.txt', 'help')
        self.write('docs/manual/manifest.json', '{}')
        self.write('docs/development/manual-consolidation/source-lock.json', '{}')
        self.commit()
        self.assertFalse(self.push()['build'])

    def test_allowlist_does_not_cover_code_configuration_or_unknown_files(self):
        for path in ['docs/tool.py', 'scripts/manual/build.py', 'src/README.md', 'Makefile',
                     'limits.conf', 'banner.txt', 'webui/templates/index.html',
                     '.github/workflows/linux.yml', 'ci/change_scope.py', 'tests/test_ci_scope.py',
                     'docs/manual/settings.conf', 'docs/manual/unknown.json', '../README.md']:
            with self.subTest(path=path):
                self.assertFalse(documentation(path))

    def test_mixed_and_multi_commit_push(self):
        self.write('src/test.c', 'changed code')
        self.commit()
        self.write('docs/guide.md', 'last commit only docs')
        self.commit()
        self.assertTrue(self.push()['build'])

    def test_more_than_api_path_limit(self):
        for n in range(310):
            self.write(f'docs/{n:04}.md', 'documentation')
        self.write('z-runtime.c', 'not documentation')
        self.commit()
        result = self.push()
        self.assertEqual(len(result['files']), 311)
        self.assertTrue(result['build'])

    def test_deletion(self):
        (self.repo / 'docs/guide.md').unlink()
        self.commit()
        self.assertFalse(self.push()['build'])
        (self.repo / 'src/test.c').unlink()
        self.commit()
        self.assertTrue(self.push()['build'])

    def test_rename_code_into_docs_still_requires_build(self):
        self.git('mv', 'src/test.c', 'docs/old-code.md')
        self.commit()
        self.assertTrue(self.push()['build'])

    def test_rename_docs_outside_allowlist_requires_build(self):
        self.git('mv', 'docs/guide.md', 'guide.conf')
        self.commit()
        self.assertTrue(self.push()['build'])

    def test_rename_documentation(self):
        self.git('mv', 'docs/guide.md', 'docs/new guide.md')
        self.commit()
        self.assertFalse(self.push()['build'])

    def test_executable_or_symlink_is_not_plain_documentation(self):
        blob = self.git('rev-parse', 'HEAD:docs/guide.md')
        for mode in ['100755', '120000']:
            self.git('update-index', '--cacheinfo', f'{mode},{blob},docs/guide.md')
            self.git('-c', 'commit.gpgsign=false', 'commit', '-qm', 'mode')
            self.assertTrue(self.push()['build'])

    def test_unknown_range_empty_diff_manual_and_release_force_full(self):
        self.assertTrue(self.push()['build'])
        self.write('docs/guide.md', 'doc update')
        self.commit()
        for base in ['0' * 40, 'f' * 40, '--bad-ref']:
            self.assertTrue(self.push(base=base)['build'])
        self.assertTrue(self.push(head=self.base)['build'])
        self.assertTrue(self.push(ref='refs/heads/release/0.75')['build'])
        self.assertTrue(self.push(ref='refs/tags/v0.75')['build'])
        for event in ['workflow_dispatch', 'workflow_run', 'unknown']:
            self.assertTrue(classify(self.repo, event, {}, 'refs/heads/main')['build'])

    def test_pr_uses_merge_base_not_advanced_target_changes(self):
        self.git('checkout', '-qb', 'feature')
        self.write('docs/guide.md', 'feature docs')
        head = self.commit()
        self.git('checkout', '--detach', self.base)
        self.write('src/test.c', 'unrelated target code')
        target = self.commit()
        self.git('checkout', 'feature')
        event = {'pull_request': {'base': {'sha': target}, 'head': {'sha': head}}}
        self.assertFalse(classify(self.repo, 'pull_request', event, 'refs/pull/1/merge')['build'])
        self.write('src/test.c', 'feature code')
        event['pull_request']['head']['sha'] = self.commit()
        self.assertTrue(classify(self.repo, 'pull_request', event, 'refs/pull/1/merge')['build'])


def workflow(name):
    return yaml.load((ROOT / '.github/workflows' / name).read_text(encoding='utf-8'), Loader=yaml.BaseLoader)


class WorkflowTests(unittest.TestCase):
    @unittest.skipUnless(os.name == 'posix', 'Workflow gates use bash on GitHub-hosted Linux')
    def test_required_gates_execute_fail_closed(self):
        scripts = {
            'linux': workflow('linux.yml')['jobs']['linux']['steps'][0]['run'],
            'native': workflow('multi-os-ci.yml')['jobs']['release-gate']['steps'][0]['run'],
        }
        with tempfile.TemporaryDirectory() as folder:
            for name, script in scripts.items():
                for scope, build, native, ok in [
                    ('success', 'true', 'success', True),
                    ('success', 'false', 'skipped', True),
                    ('failure', 'false', 'skipped', False),
                    ('cancelled', 'true', 'success', False),
                    ('success', '', 'skipped', False),
                    ('success', 'true', 'skipped', name == 'linux'),
                    ('success', 'false', 'success', name == 'linux'),
                    ('success', 'true', 'failure', name == 'linux'),
                ]:
                    with self.subTest(name=name, scope=scope, build=build, native=native):
                        env = dict(os.environ, SCOPE_RESULT=scope, BUILD=build, NATIVE_RESULT=native,
                                   GITHUB_STEP_SUMMARY=str(Path(folder) / 'summary'))
                        run = subprocess.run(['bash', '-e', '-c', script], env=env, capture_output=True)
                        self.assertEqual(run.returncode == 0, ok, run.stderr)

    def test_all_automatic_builds_share_the_classifier(self):
        for name, job in [('linux.yml', 'linux'), ('rocky-package.yml', 'build-rpm'),
                          ('debian-package.yml', 'build-deb'), ('freebsd-package.yml', 'package'),
                          ('multi-os-ci.yml', 'native'), ('shared-visibility.yml', 'integration')]:
            doc = workflow(name)
            self.assertEqual(doc['jobs']['changes']['uses'], './.github/workflows/change-scope.yml')
            self.assertEqual(doc['jobs'][job]['needs'], 'changes')
            if 'pull_request' in doc['on']:
                self.assertFalse(doc['on']['pull_request'])  # no workflow path filter
            if name != 'linux.yml':
                self.assertIn("needs.changes.outputs.build == 'true'", doc['jobs'][job]['if'])

    def test_required_linux_contexts_remain_and_fail_on_scope_failure(self):
        job = workflow('linux.yml')['jobs']['linux']
        self.assertEqual(job['if'], '${{ always() }}')
        self.assertEqual(job['strategy']['matrix']['image'], ['rockylinux/rockylinux:9', 'almalinux:9'])
        guard = job['steps'][0]['run']
        self.assertIn('test "$SCOPE_RESULT" = success', guard)
        self.assertIn('case "$BUILD" in true|false)', guard)
        self.assertIn("needs.changes.outputs.build == 'true'", job['steps'][-1]['if'])

    def test_manual_still_runs_and_release_set_uses_same_run_receipt(self):
        self.assertIn('manual', workflow('manual.yml')['jobs'])
        doc = workflow('release-set.yml')
        steps = doc['jobs']['changes']['steps']
        self.assertEqual(steps[0]['with']['run-id'], '${{ github.event.workflow_run.id }}')
        self.assertEqual(steps[0]['with']['name'], 'ci-change-scope')
        self.assertIn("receipt['source_sha'] == os.environ['SOURCE_SHA']", steps[1]['run'])
        self.assertIn("needs.changes.outputs.build == 'true'", doc['jobs']['verify']['if'])

    def test_native_skips_only_when_scope_succeeds_and_explicitly_says_false(self):
        job = workflow('multi-os-ci.yml')['jobs']['release-gate']
        self.assertEqual(job['needs'], ['changes', 'native'])
        script = job['steps'][0]['run']
        self.assertIn('test "$SCOPE_RESULT" = success', script)
        self.assertIn('test "$NATIVE_RESULT" = skipped', script)
        self.assertIn('test "$BUILD" = true', script)
        self.assertIn('test "$NATIVE_RESULT" = success', script)


if __name__ == '__main__':
    unittest.main()
