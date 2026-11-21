import json
import os
import stat
import tempfile
import unittest
from datetime import datetime, timedelta
from unittest import mock

import buddy_link as bl


def every_5_s(start, end, *unlocked):
    """The helper's looks at the lock from `start` to `end`, as (when, locked):
    unlocked inside the (from, to) spans given."""
    when, end = datetime.fromisoformat(start), datetime.fromisoformat(end)
    spans = [(datetime.fromisoformat(begin), datetime.fromisoformat(stop)) for begin, stop in unlocked]
    while when <= end:
        yield when, not any(begin <= when < stop for begin, stop in spans)
        when += timedelta(seconds=5)


class Days(unittest.TestCase):
    def setUp(self):
        folder = self.enterContext(tempfile.TemporaryDirectory())
        self.enterContext(mock.patch.object(bl, 'DAYS_FILE', os.path.join(folder, 'monitor-buddy', 'days.csv')))

    def follow(self, looks):
        """The days the helper notes over `looks`, started afresh as after a restart."""
        days, look = bl.read_days(), None
        for when, is_locked in looks:
            bl.note_lock(days, look, is_locked, when)
            look = is_locked, when
        return days

    def test_first_unlock_and_last_lock(self):
        days = self.follow(every_5_s('2026-11-20 08:40', '2026-11-20 18:00',
                                     ('2026-11-20 08:47', '2026-11-20 12:30'), ('2026-11-20 13:15', '2026-11-20 17:32')))
        self.assertEqual(bl.day_times(days, datetime(2026, 11, 20, 18)), ['08:47', '17:32', '', ''])
        with open(bl.DAYS_FILE) as file:
            self.assertEqual(file.read(), 'date,arrived,left\n2026-11-20,08:47,17:32\n')
        self.assertEqual(stat.S_IMODE(os.stat(bl.DAYS_FILE).st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(os.stat(os.path.dirname(bl.DAYS_FILE)).st_mode), 0o700)

    def test_a_lock_after_midnight_counts_for_the_day_before(self):
        days = self.follow(every_5_s('2026-11-19 08:40', '2026-11-20 09:00', ('2026-11-19 08:47', '2026-11-19 17:32'),
                                     ('2026-11-19 22:00', '2026-11-20 01:00'), ('2026-11-20 08:30', '2026-11-20 10:00')))
        self.assertEqual(bl.day_times(days, datetime(2026, 11, 20, 9)), ['08:30', '', '08:47', '01:00'])

    def test_a_restart_keeps_the_arrival(self):
        self.follow(every_5_s('2026-11-20 08:40', '2026-11-20 10:00', ('2026-11-20 08:47', '2026-11-20 11:00')))
        days = self.follow(every_5_s('2026-11-20 10:01', '2026-11-20 11:30', ('2026-11-20 08:47', '2026-11-20 11:00')))
        self.assertEqual(days['2026-11-20'], ['08:47', '11:00'])
        days = self.follow(every_5_s('2026-11-20 11:31', '2026-11-20 12:30', ('2026-11-20 12:00', '2026-11-20 13:00')))
        self.assertEqual(days['2026-11-20'], ['08:47', '11:00'])   # restarted while locked: no lock seen happening

    def test_a_lock_on_the_way_to_sleep_counts_when_it_went_to_sleep(self):
        looks = [(datetime(2026, 11, 20, 17, 59, 55), False), (datetime(2026, 11, 21, 8, 30), True),
                 (datetime(2026, 11, 21, 8, 31), False)]
        days = self.follow(looks)
        self.assertEqual(bl.day_times(days, datetime(2026, 11, 21, 9)), ['08:31', '', '17:59', '17:59'])


class Fit(unittest.TestCase):
    def test_long_titles_give_way(self):
        sessions = [['folder', 0, 1763600000]] * 4
        state = {'locked': False, 'sessions': list(sessions), 'pr_list': [{'t': '#1 ' + 'é' * 47, 's': 1, 'r': 0}] * 4}
        bl.fit(state)
        self.assertLessEqual(len(json.dumps(state)), bl.PACKET_MAX)
        self.assertEqual(state['sessions'], sessions)
        self.assertIn(len(state['pr_list']), (1, 2, 3))


if __name__ == '__main__':
    unittest.main()
