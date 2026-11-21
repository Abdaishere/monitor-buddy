"""A whole workday, 07:55 on Monday to 08:02 on Tuesday: every moment workday.py films, checked to the pixel."""
import pytest

from workday import START, run_workday


@pytest.mark.workday
def test_a_whole_workday(make_buddy, golden):
    """Power-on, briefing, focus, Claude, CI, meetings, lunch, away, full day, day done, night, next morning."""
    b = make_buddy(when=START)
    seen = {}

    def watch(name, caption, seconds):
        assert name not in seen, 'two moments of the day share the name ' + name
        seen[name] = {'light': b.board()['backlight'], 'app': b.status_line().get('app'), 'at': b.now.strftime('%a %H:%M')}
        golden('workday_' + name, b.shot())
        b.run(seconds)          # the film stays on the moment this long; the test lives the same day

    laptop = run_workday(b, watch)

    assert len(seen) >= 45
    assert 'join' in laptop.asked, 'the tap at 10:26 asks the laptop to open the meeting'
    assert seen['night']['light'] < seen['before_night']['light'], 'the night window dims the screen'
    assert seen['night_wake']['light'] > seen['night']['light'], 'a tap at night lights it up'
    assert seen['night_again']['light'] == seen['night']['light']
    assert seen['night_3am']['light'] == seen['night']['light']
    assert seen['morning']['light'] > seen['night']['light'], 'the night ends at 07:00'
    assert seen['tuesday_today']['at'] == 'Tue 08:02'
    assert not b.restarted, 'no restart in 24 hours'
    assert 'Guru Meditation' not in b.log and 'abort()' not in b.log
