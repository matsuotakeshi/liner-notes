"""Portable checks for the Solaris reserved UID release guard."""
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'solaris'),str(ROOT/'scripts')]
import door_solaris_slot as slots


class SolarisSlot(unittest.TestCase):
    def test_snapshot_detects_all_three_uid_fields(self):
        for row in ('61001 0 0 S','0 61001 0 S','0 0 61001 S'):
            self.assertTrue(slots.snapshot_contains('0 0 0 S\n'+row+'\n',61001))
        self.assertFalse(slots.snapshot_contains('0 0 0 S\n61001 - - Z\n',61001))
        for rows in ('','61001 0 0 S\n0 0 - S','bad output','0 0 0'):
            with self.subTest(rows=rows),self.assertRaises(OSError):
                slots.snapshot_contains(rows,61001)

    def test_no_release_after_a_failed_stop_or_unknown_process_state(self):
        for stop, active, failure in ((Mock(side_effect=OSError),False,OSError),
                                      (Mock(),True,slots.SlotBusy),
                                      (Mock(),OSError('unknown snapshot'),OSError)):
            with self.subTest(active=active):
                lease = Mock(fd=9)
                reservation = slots.Reservation(lease,61001)
                with patch.object(slots,'_active') as snapshot, \
                     patch.object(slots.os,'ftruncate') as truncate:
                    if isinstance(active,Exception):
                        snapshot.side_effect=active
                    else:
                        snapshot.return_value=active
                    with self.assertRaises(failure):
                        reservation.stop_and_release(stop)
                    truncate.assert_not_called()
                    lease.close.assert_not_called()
                reservation.close()
                lease.close.assert_called_once()

    def test_owned_stop_waits_for_the_last_slot_uid_to_disappear(self):
        lease = Mock(fd=9)
        stop = Mock()
        reservation = slots.Reservation(lease,61001)
        with (
            patch.object(slots, '_active', side_effect=[True,False]) as snapshot,
            patch.object(slots.time, 'monotonic', side_effect=[10,10.01]),
            patch.object(slots.time, 'sleep') as sleep,
            patch.object(slots.os, 'ftruncate') as truncate,
            patch.object(slots.os, 'fsync') as sync,
        ):
            reservation.stop_and_release(stop, settle=2)
        stop.assert_called_once()
        self.assertEqual(snapshot.call_count,2)
        sleep.assert_called_once_with(0.05)
        truncate.assert_called_once_with(9,0)
        sync.assert_called_once_with(9)
        lease.close.assert_called_once()


if __name__=='__main__':unittest.main()
