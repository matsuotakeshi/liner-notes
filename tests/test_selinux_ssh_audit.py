"""Strict SSH audit acceptance for the Hosted SELinux fixture (no MAC claim)."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'ci'))
from selinux_ssh import check_bootstrap_ssh_audit


class SSHAuditTests(unittest.TestCase):
    def audit(self, slots=(0, 0, 0, 0)):
        return ''.join(f'SSH auth {status} slot={slot}\n' for status, slot in zip(
            ('rejected', 'accepted', 'accepted', 'accepted'), slots))

    def test_exact_events_allow_slot_reuse_or_delayed_cleanup(self):
        for slots in ((0, 0, 0, 0), (0, 1, 2, 3), (4, 0, 8, 0)):
            check_bootstrap_ssh_audit(self.audit(slots))

    def test_missing_and_extra_events_rejected(self):
        good = self.audit()
        for output in ('', '\n'.join(good.splitlines()[:-1]),
                       good + 'SSH auth accepted slot=0\n', good + '\n'):
            with self.subTest(output=output), self.assertRaises(AssertionError):
                check_bootstrap_ssh_audit(output)

    def test_wrong_status_or_order_rejected(self):
        good = self.audit()
        for output in (good.replace('rejected', 'accepted'),
                       good.replace('accepted', 'rejected', 1),
                       '\n'.join(reversed(good.splitlines()))):
            with self.assertRaises(AssertionError):
                check_bootstrap_ssh_audit(output)

    def test_malformed_events_and_secret_output_rejected_without_echo(self):
        good = self.audit()
        for replacement in ('slot=-1', 'slot=00', 'slot=x', 'slot=0 extra',
                            'slot=0 password=SYNTHETIC_SECRET', 'slot=0\rsecret',
                            'slot=0\x00', 'slot=١'):
            with self.subTest(replacement=replacement), self.assertRaises(AssertionError) as failure:
                check_bootstrap_ssh_audit(good.replace('slot=0', replacement, 1))
            self.assertNotIn('SYNTHETIC_SECRET', str(failure.exception))
        with self.assertRaises(AssertionError):
            check_bootstrap_ssh_audit(good.replace('SSH auth rejected', 'unexpected runtime error', 1))


if __name__ == '__main__':
    unittest.main()
