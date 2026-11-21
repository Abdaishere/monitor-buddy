"""The same workday on a Buddy that was made personal: every "You and me" setting on, on the owner's birthday."""
import re

import pytest

from workday import START, run_workday

PERSONAL = {'name': 'Sam', 'buddy_name': 'Pixel', 'greeting': 'custom', 'greeting_text': 'Hi Sam, all good?',
            'work': {'start': 8, 'end': 17, 'lunch': 12}, 'weekend': 'sat-sun', 'birthday': '01-05', 'since': '2025-01-05',
            'break_min': 20, 'water': True, 'message': 'Ship it', 'face': 'cat'}


@pytest.mark.workday
def test_a_personal_workday(make_buddy):
    """Names, an own greeting, birthday, anniversary, stretch and water reminders, a message, the cat face: a day without a hitch."""
    b = make_buddy(when=START)
    b.run(10)
    answer = b.mb_restart('SET', {'config': PERSONAL})
    assert answer and answer.get('ok'), answer
    b.run(12)
    saved = b.mb('GET')['config']
    assert {k: saved[k] for k in PERSONAL} == PERSONAL
    mark = b.mark()
    seen = []

    def watch(name, caption, seconds):
        seen.append(name)
        b.run(seconds)

    b.restarted = False
    run_workday(b, watch)
    log = b.log_since(mark)
    assert not b.restarted, 'no restart in 24 hours'
    for bad in ('Guru Meditation', 'abort()', 'Stack canary', 'assert failed'):
        assert bad not in log
    statuses = set(re.findall(r'^mood: status (\d+)', log, re.M))
    # birthday, stretch, water, lunch time, welcome back, the own message
    missing = {'17', '19', '20', '21', '23', '24'} - statuses
    assert not missing, 'personal statuses that never showed: %s' % sorted(missing)
    assert 'Hi Sam, all good?' in log, 'the own greeting answers the double knock'
    assert len(seen) >= 45
