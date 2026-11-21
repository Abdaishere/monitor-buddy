"""The face's statuses from real causes, and their order: docs/HANDOVER.md §7.4."""
from datetime import timedelta

from conftest import odoo_data, pc_state


def status(b, mark):
    """The status the face last changed to since `mark`, as 'N CAPTION'."""
    lines = [line[len('mood: status '):] for line in b.log_since(mark).split('\n') if line.startswith('mood: status ')]
    return lines[-1] if lines else None


def test_on_air_while_the_microphone_records(at_work, golden):
    """The microphone goes on: ON AIR with the time in the meeting; off: back to normal, the minutes counted."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.state(**pc_state(mic=True))
    b.run(2)
    assert status(b, mark).startswith('11 IN A MEETING')
    assert b.page() == 0, 'the status holds the face'
    for _ in range(9):          # the laptop sends its state every minute at least
        b.run(20)
        b.state(**pc_state(mic=True))
    golden('face_on_air', b.shot())
    b.state(**pc_state(mic=False))
    b.run(2)
    assert status(b, mark).startswith('0')
    assert 170 <= b.read_json('/day.json')['meet'] <= 190


def test_meeting_soon_and_join(at_work, golden):
    """From 5 min before a meeting: MEETING IN n MIN; a tap asks the laptop to open its link."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.odoo(**odoo_data(b, dev=False, meetings=[('Weekly sync', 8, 30)]))
    meeting = [['Weekly sync', b.true_ms // 1000 + 480, b.true_ms // 1000 + 480 + 1800]]

    def minute():
        b.run(60)
        b.state(**pc_state())
        b.odoo(**odoo_data(b, dev=False, m=meeting))

    minute()
    minute()
    assert status(b, mark) is None, 'six minutes ahead is too early'
    minute()
    b.run(2)
    assert status(b, mark) == '12 MEETING IN 5 MIN', status(b, mark)
    golden('face_meeting_soon', b.shot())
    b.tap()
    assert 'tap: join the meeting' in b.log_since(mark)
    golden('face_meeting_opening_link', b.shot())
    assert b.status_bits() & 4, 'the next ping answer asks the laptop to join'
    assert not b.status_bits() & 4
    for _ in range(4):
        minute()
    b.run(35)
    golden('face_meeting_now', b.shot())
    minute()
    minute()
    assert status(b, mark).startswith('0'), 'a minute after the start the warning ends'


def test_lunch_and_away(make_buddy, golden):
    """Locked between 12:00 and 14:00: LUNCH BREAK; locked at another time: AWAY."""
    from mbsim import MONDAY_8AM
    b = make_buddy(when=MONDAY_8AM.replace(hour=12, minute=20))
    b.run(12)
    b.state(**pc_state())
    b.run(40)
    mark = b.mark()
    b.state(**pc_state(locked=True))
    b.run(2)
    assert status(b, mark) == '2 LUNCH BREAK'
    golden('face_lunch', b.shot())
    b.run_until('14:01', each=lambda: b.state(**pc_state(locked=True)), step_s=50)
    b.run(2)
    assert status(b, mark) == '3 AWAY'
    golden('face_away', b.shot())
    b.state(**pc_state(locked=False))
    b.run(2)
    assert status(b, mark) == '23 OH! HI', 'back after 10 minutes or more: a welcome for 3 s'
    b.run(4)
    assert status(b, mark).startswith('0')


def test_claude_needs_you_then_done(at_work, golden):
    """A Claude Code session waits: CLAUDE NEEDS YOU with its folder; it finishes: CLAUDE DONE."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.state(**pc_state(claude=1, at='monitor-buddy'))
    b.run(2)
    assert status(b, mark) == '9 CLAUDE NEEDS YOU'
    golden('face_claude_needs', b.shot())
    b.run(2.1)
    golden('face_claude_needs_project', b.shot())
    b.state(**pc_state(claude=2, at='monitor-buddy'))
    b.run(2)
    assert status(b, mark) == '10 CLAUDE DONE'
    golden('face_claude_done', b.shot())
    b.state(**pc_state(claude=0))
    b.run(2)
    assert status(b, mark).startswith('0')


def test_ci_red_then_green_plays_the_animation(at_work, golden):
    """A failing pull request: CI RED: n; the last one passes: the ci-green animation plays for 7 s."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.state(**pc_state(prs_failing=2))
    b.run(2)
    assert status(b, mark) == '5 CI RED: 2'
    golden('face_ci_red', b.shot())
    b.state(**pc_state(prs_failing=0))
    b.run(1.5)
    assert status(b, mark).startswith('0')
    green = b.shot()
    golden('gif_ci_green', green)
    b.run(8)
    after = b.shot()
    golden('face_after_ci_green', after)
    from mbsim import diff
    assert diff(green, after)[0] > 2000, 'the animation covered the face, then left'


def test_log_your_hours_nudge(at_work, golden):
    """A weekday with the day before under half the hours: LOG <DAY>'S HOURS."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.odoo(**odoo_data(b, before=2.0, dev=False))
    b.run(2)
    assert status(b, mark) == "4 LOG FRIDAY'S HOURS"
    golden('face_log_hours', b.shot())
    b.odoo(**odoo_data(b, before=7.5, dev=False))
    b.run(2)
    assert status(b, mark).startswith('0')


def test_locked_in_on_a_development_task(at_work, golden):
    """A task in a development stage: LOCKED IN."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.odoo(**odoo_data(b, dev=True))
    b.run(2)
    assert status(b, mark) == '6 LOCKED IN'
    golden('face_locked_in', b.shot())


def test_status_priority(at_work):
    """Higher statuses win: on air over Claude over CI red over locked in, and each returns when the one above ends."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.odoo(**odoo_data(b, dev=True))
    b.state(**pc_state(prs_failing=1))
    b.run(2)
    assert status(b, mark) == '5 CI RED: 1'
    b.state(**pc_state(prs_failing=1, claude=1, at='odoo'))
    b.run(2)
    assert status(b, mark) == '9 CLAUDE NEEDS YOU'
    b.state(**pc_state(prs_failing=1, claude=1, at='odoo', mic=True))
    b.run(2)
    assert status(b, mark).startswith('11 IN A MEETING')
    b.double_knock()
    assert status(b, mark).startswith('15 ')
    b.run(15)
    b.state(**pc_state(prs_failing=1, claude=1, at='odoo', mic=True))
    b.run(1)
    assert status(b, mark).startswith('11 IN A MEETING')
    b.state(**pc_state(prs_failing=1, claude=1, at='odoo'))
    b.run(2)
    assert status(b, mark) == '9 CLAUDE NEEDS YOU'
    b.state(**pc_state(prs_failing=1))
    b.run(2)
    assert status(b, mark) == '5 CI RED: 1'
    b.state(**pc_state())
    b.run(9)
    assert status(b, mark) == '6 LOCKED IN'


def test_stale_laptop_state_is_dropped(at_work):
    """With no word from the laptop for 3 minutes its state is forgotten: CI RED goes away."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.state(**pc_state(prs_failing=2))
    b.run(2)
    assert status(b, mark) == '5 CI RED: 2'
    b.run(200)
    assert status(b, mark).startswith('0')


def test_full_day_of_hours_plays_the_animation(at_work, golden):
    """Odoo hours reach the day's 8: the full-day animation plays."""
    b = at_work
    b.show_page(1)
    b.odoo(**odoo_data(b, hours=7.9, dev=False))
    b.run(2)
    before = b.shot()
    b.odoo(**odoo_data(b, hours=8.1, dev=False))
    b.run(1.5)
    from mbsim import diff
    assert diff(before, b.shot())[0] > 2000, 'the animation covers the clock'
    golden('gif_full_day', b.shot())
    b.run(10)


def test_focus_done_animation_after_25_minutes(at_work):
    """A focus session of 25 min or more ends with the focus-done animation and counts for the day."""
    b = at_work
    b.show_page(0)
    b.lay_flat()
    for _ in range(26):
        b.run(60)
        b.state(**pc_state())
    b.stand_up()
    b.run(1.5)
    playing = b.shot()
    b.run(9)
    from mbsim import diff
    assert diff(playing, b.shot())[0] > 2000, 'the focus-done animation played over the face'
    day = b.read_json('/day.json')
    assert 1550 <= day['focus'] <= 1570 and day['sessions'] == 1


def test_screensaver_after_30_minutes_away(at_work):
    """AWAY for 30 minutes starts the bounce screensaver; unlocking stops it."""
    b = at_work
    mark = b.mark()
    for _ in range(31):
        b.state(**pc_state(locked=True))
        b.run(60)
    assert status(b, mark) == '3 AWAY'
    face = b.shot()
    b.state(**pc_state(locked=True))
    b.run(30)
    from mbsim import diff
    saver = b.shot()
    assert diff(face, saver)[0] > 500
    b.state(**pc_state(locked=False))
    b.run(2)
    assert status(b, mark) == '23 OH! HI', 'back after 10 minutes or more: a welcome for 3 s'
    b.run(4)
    assert status(b, mark).startswith('0')


def test_notice_from_buddy_say(at_work, golden):
    """buddy-say: the line shows over any page for 20 s; a tap puts it away."""
    b = at_work
    b.show_page(1)
    assert b.say('Deploy finished: all 212 tests pass on staging')
    b.run(1)
    golden('notice', b.shot())
    b.run(21)
    b.say('Second notice')
    b.run(1)
    with_notice = b.shot()
    b.tap()
    from mbsim import diff
    assert diff(with_notice, b.shot())[0] > 500, 'the tap put the notice away'


def test_demo_statuses_match_the_guide(at_work, golden):
    """STATE demo forces each status, as the guide's screenshots are taken."""
    b = at_work
    b.show_page(0)
    for number in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16):
        b.show_page(0)          # demo mode never holds the face
        b.state(**pc_state(demo=number))
        b.run(2)
        golden('demo_status_%02d' % number, b.shot())
    b.state(**pc_state(demo=0))
