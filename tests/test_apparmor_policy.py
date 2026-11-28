#!/usr/bin/env python3
"""Source checks for the optional Debian AppArmor policy package."""
from pathlib import Path
import importlib.util
import re
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / 'linux/apparmor'
PROFILES = {
    'liner-notes', 'liner-notes-web', 'liner-notes-link', 'liner-notes-admin',
    'liner-notes-transfer', 'liner-notes-door-broker',
    'liner-notes-door-launcher', 'liner-notes-door',
}
UNITS = {
    'liner-notes', 'liner-notes-web', 'liner-notes-link',
    'liner-notes-door-broker', 'liner-notes-door-isolation',
}

def native_fixture():
    spec = importlib.util.spec_from_file_location('apparmor_root', ROOT/'ci/apparmor_root.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    return helper


class Policy(unittest.TestCase):
    def test_named_only_and_explicit_transitions(self):
        profiles = list((POLICY / 'profiles').iterdir())
        self.assertEqual({path.name for path in profiles}, PROFILES)
        for path in profiles:
            with self.subTest(path=path.name):
                text = path.read_text()
                self.assertEqual(re.findall(r'^profile (.+) \{$', text, re.M), [path.name])
                self.assertIn(f'include if exists <local/{path.name}>', text)
                rules = '\n'.join(line for line in text.splitlines() if not line.lstrip().startswith('#'))
                self.assertNotRegex(rules, r'(?i)\bu[xX]\b|\b[pPcC][iI][xX]\b')
                self.assertNotRegex(rules, r'(?m)^\s*/(?:\*\*|opt/\*\*|var/lib/\*\*|etc/liner-notes/\*\*)\s')
                for target in re.findall(r'-> ([a-z-]+),', rules):
                    self.assertIn(target, PROFILES)
        self.assertIn('Px -> liner-notes-transfer,', (POLICY/'profiles/liner-notes').read_text())
        self.assertNotIn('-> liner-notes-door,', (POLICY/'profiles/liner-notes-door-launcher').read_text())
        self.assertIn('Px -> liner-notes-door,', (POLICY/'examples/door-launcher.local').read_text())

    def test_separate_private_paths(self):
        for role in ('web', 'link', 'door', 'transfer', 'door-broker', 'door-launcher'):
            text = (POLICY / f'profiles/liner-notes-{role}').read_text()
            text = '\n'.join(line for line in text.splitlines()
                             if not line.lstrip().startswith('audit deny '))
            self.assertNotIn('/var/lib/liner-notes/{', text)
            self.assertNotIn('/etc/liner-notes/ssh', text)
            if role != 'link':
                self.assertNotIn('/var/lib/liner-notes-link/identity', text)
                self.assertNotIn('link-secrets', text)
        self.assertNotIn('/var/backups/', (POLICY/'profiles/liner-notes').read_text())

    def test_native_fixture_keeps_project_paths_and_names_private(self):
        helper = native_fixture()
        names = {name: 'ln-aa-ci-dummy-' + name for name in PROFILES}
        sources = [(POLICY/'profiles'/name).read_text() for name in sorted(PROFILES)]
        result = helper.assemble(sources, names, Path('/tmp/ln-aa-ci-dummy'), '/tmp/probe')
        self.assertEqual(result.count('abi <abi/3.0>,'), 1)
        self.assertEqual(set(re.findall(r'^profile (.+) \{$', result, re.M)), set(names.values()))
        self.assertIn('/tmp/ln-aa-ci-dummy/var/lib/liner-notes/', result)
        self.assertIn('-> ln-aa-ci-dummy-liner-notes-transfer,', result)
        self.assertNotIn('include if exists <local/', result)
        self.assertNotRegex(result, r'(?m)^  (?:audit deny )?/(?:etc|var/lib|var/log|run)/liner-notes')

    def test_native_fetch_has_https_and_time_bounds(self):
        helper = native_fixture()
        url = 'https://raw.githubusercontent.com/matsuotakeshi/liner-notes/example'
        with patch.object(helper, 'run', return_value='bounded') as invoke:
            self.assertEqual(helper.get(url, 16384), 'bounded')
        args = invoke.call_args.args
        self.assertEqual(args[:2], ('/usr/bin/curl', '-q'))
        for flag, value in (('--proto', '=https'), ('--connect-timeout', '5'),
                            ('--max-time', '15'), ('--max-filesize', '16384')):
            self.assertEqual(args[args.index(flag)+1], value)
        self.assertEqual(args[-2:], ('--', url))

    def test_native_denial_retries_until_matching_audit_event(self):
        helper = native_fixture()
        event = 'apparmor="DENIED" profile="profile" name="/fixture/data" denied_mask="r"'
        with patch.object(helper, 'probe_run') as probe, patch.object(helper.time, 'sleep') as sleep, \
             patch.object(helper, 'run', side_effect=[''] * 4 + [event]) as journal:
            helper.audited_denial('profile', 'unit', '/fixture/probe', 'r', '/fixture/data')
        self.assertEqual(probe.call_count, 2)
        self.assertEqual(journal.call_count, 5)
        self.assertEqual([call.args for call in sleep.call_args_list].count((6,)), 2)

    def test_opt_in_inactive_package_layout(self):
        self.assertFalse(list((ROOT / 'debian').glob('liner-notes-apparmor.*inst')))
        manifest = (ROOT / 'debian/liner-notes-apparmor.install').read_text()
        self.assertEqual(set(manifest.splitlines()), {
            'etc/apparmor.d/*', 'usr/share/liner-notes-apparmor/*'
        })
        examples = list((POLICY / 'systemd').glob('*.service.conf'))
        self.assertEqual({p.name.removesuffix('.service.conf') for p in examples}, UNITS)
        for path in examples:
            unit = path.name.removesuffix('.conf')
            name = unit.removesuffix('.service').replace('door-isolation', 'door-launcher')
            text = path.read_text()
            self.assertIn('AssertSecurity=apparmor', text)
            self.assertIn(f'AppArmorProfile={name}\n', text)
            self.assertNotIn('AppArmorProfile=', (ROOT/'linux'/unit).read_text())
        launcher = (POLICY/'systemd/liner-notes-door-isolation.service.conf').read_text()
        self.assertIn('RestrictAddressFamilies=\nRestrictAddressFamilies=AF_UNIX', launcher)


if __name__ == '__main__':
    unittest.main()
