"""Fingers, the BOOT button and motion: docs/HANDOVER.md §3.3."""
import re


def test_swipe_changes_the_page(at_work):
    """A sideways swipe moves one page on, and back."""
    b = at_work
    first = b.page()
    b.swipe_left()
    second = b.page()
    assert second != first
    b.swipe_right()
    assert b.page() == first


def test_tap_on_the_face_cycles_moods(at_work):
    """A tap on the face shows the next of moods 0-8."""
    b = at_work
    b.show_page(0)
    start = b.mood()
    b.tap()
    assert b.mood() == (start + 1) % 9
    for _ in range(8):
        b.tap()
    assert b.mood() == start, 'nine taps come back round'


def test_double_tap_switches_auto_flipping(at_work, golden):
    """A double tap turns the automatic page flipping off (AUTO: OFF), another one on again."""
    b = at_work
    b.show_page(1)
    b.run(31)
    b.double_tap()
    golden('banner_auto_off', b.shot())
    page = b.page()
    b.run(40)
    assert b.page() == page, 'no flipping while it is off'
    b.double_tap()
    b.run(20)
    assert b.page() != page


def test_pages_flip_every_8_seconds(at_work):
    """Left alone, the round moves on every 8 s."""
    b = at_work
    mark = b.mark()
    b.run(80)
    pages = [int(p) for p in re.findall(r'^app=(\d+)', b.log_since(mark), re.M)]
    changes = sum(1 for a, c in zip(pages, pages[1:]) if a != c)
    assert 8 <= changes <= 11, pages


def test_vertical_swipe_sets_brightness(at_work, golden):
    """A swipe down dims by one step of 24, shows BRIGHTNESS n%, and saves to /display.json after 2 s."""
    b = at_work
    assert b.board()['backlight'] == 180
    b.swipe_down()
    assert b.board()['backlight'] == 156
    golden('banner_brightness', b.shot())
    b.run(3)
    assert b.read_json('/display.json') == {'brightness': 156}
    b.swipe_up(distance=120)
    assert b.board()['backlight'] == 204, 'two steps for 110 px or more'


def test_brightness_stays_in_range(at_work):
    """Brightness never leaves 8-255 however long the swipes."""
    b = at_work
    for _ in range(12):
        b.swipe_down(distance=120)
    assert b.board()['backlight'] == 8
    for _ in range(14):
        b.swipe_up(distance=120)
    assert b.board()['backlight'] == 255


def test_brightness_survives_a_restart(at_work):
    """The saved brightness comes back after a power cut."""
    b = at_work
    b.swipe_down()
    b.run(3)
    b.power_cycle()
    b.run(12)
    assert b.board()['backlight'] == 156


def test_boot_button_asks_to_lock(at_work):
    """BOOT sets bit 3 of the next ping answer, once; after 10 s unasked it is dropped."""
    b = at_work
    b.press_boot()
    assert 'boot: lock the PC' in b.log
    assert b.status_bits() & 8
    assert not b.status_bits() & 8, 'one answer carries it'
    b.press_boot()
    b.run(11)
    assert not b.status_bits() & 8, 'a request older than 10 s is dropped'


def test_boot_button_is_debounced(at_work):
    """A bouncing BOOT press counts once."""
    b = at_work
    mark = b.mark()
    for _ in range(4):
        b.cmd('boot 1')
        b.run_ms(20)
        b.cmd('boot 0')
        b.run_ms(20)
    b.run(1)
    assert b.log_since(mark).count('boot: lock the PC') == 1


def test_double_knock_greets(at_work, golden):
    """Knock twice: a greeting on the face for 2 s, then the Today page for 12 s."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.double_knock()
    log = b.log_since(mark)
    assert log.count('knock: 0.') == 2 and 'knock: two knocks' in log
    assert re.search(r"mood: status 15 (WHAT'S UP\?|HELLO!|YES\?|HEY THERE!|WHO'S THERE\?)", log)
    golden('face_greeting', b.shot())
    b.run(2.2)
    assert b.page() == 12, 'the Today page follows the greeting'
    b.run(13)
    assert b.page() != 12
    assert b.health()['double_knocks'] == 1


def test_single_knock_is_no_greeting(at_work):
    """One knock alone does nothing but a blink."""
    b = at_work
    mark = b.mark()
    b.knock()
    b.run(2)
    log = b.log_since(mark)
    assert 'knock: 0.' in log and 'two knocks' not in log and 'status 15' not in log


def test_typing_is_no_knock(at_work):
    """A run of small jolts (typing on the desk) never counts as a double knock."""
    b = at_work
    mark = b.mark()
    for i in range(60):
        b.knock(g=0.12 + (i % 3) * 0.04, ms=6)
        b.run_ms(90 + (i * 37) % 60)
    b.run(2)
    assert 'two knocks' not in b.log_since(mark)


def test_three_knocks_cancel(at_work):
    """Three hard knocks in a row are handling, not a knock-knock."""
    b = at_work
    mark = b.mark()
    b.run_ms(400)
    for _ in range(3):
        b.knock()
        b.run_ms(200)
    b.run(2)
    assert 'two knocks' not in b.log_since(mark)


def test_knock_with_a_finger_on_the_screen_is_ignored(at_work):
    """A double knock while a finger is on the glass does not greet."""
    b = at_work
    mark = b.mark()
    b.touch(300, 20)
    b.run_ms(500)
    b.knock()
    b.run_ms(260)
    b.knock()
    b.run_ms(600)
    b.release()
    b.run(1)
    assert 'two knocks' not in b.log_since(mark)


def test_shake_makes_it_dizzy(at_work, golden):
    """A shake: WHOAAA, then REALLY?, then the Today page."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.shake()
    b.run(0.4)
    assert 'mood: status 13 WHOAAA' in b.log_since(mark)
    golden('face_dizzy', b.shot())
    b.run(1.6)
    assert 'mood: status 14 REALLY?' in b.log_since(mark)
    golden('face_really', b.shot())
    b.run(3)
    assert b.page() == 12


def test_spin_surprises(at_work):
    """A fast spin about the screen's axis: the surprised face."""
    b = at_work
    b.show_page(0)
    b.spin()
    assert b.mood() == 2


def test_lay_flat_starts_the_focus_timer(at_work, golden):
    """Lay it flat for 2 s: the focus timer starts; stand it up: it stops and shows FOCUSED for 15 s."""
    b = at_work
    b.show_page(0)
    mark = b.mark()
    b.lay_flat()
    b.run(4)
    assert 'mood: status 7 FOCUS' in b.log_since(mark)
    assert b.status_line()['timer'] == '1'
    assert b.status_bits() & 1, 'the link answer says the timer runs'
    b.run(96)
    golden('face_focus', b.shot())
    b.stand_up()
    b.run(2)
    assert 'mood: status 8 FOCUSED' in b.log_since(mark)
    assert b.status_line()['timer'] == '0'
    golden('face_focused', b.shot())
    b.run(16)
    assert 'mood: status 0' in b.log_since(mark)
    day = b.read_json('/day.json')
    assert 95 <= day['focus'] <= 105 and day['sessions'] == 1


def test_short_focus_does_not_count(at_work):
    """A focus session under a minute adds nothing to the day."""
    b = at_work
    b.lay_flat()
    b.run(30)
    b.stand_up()
    b.run(20)
    day = b.read_json('/day.json') or {}
    assert day.get('focus', 0) == 0 and day.get('sessions', 0) == 0


def test_locking_the_laptop_stops_the_timer(at_work):
    """Locking the laptop stops a running timer, even one the Buddy started lying flat."""
    b = at_work
    from conftest import pc_state
    b.lay_flat()
    b.run(70)
    assert b.status_line()['timer'] == '1'
    b.state(**pc_state(locked=True))
    b.run(2)
    assert b.status_line()['timer'] == '0'


def test_touch_is_locked_while_the_laptop_is(at_work):
    """Privacy lock: with the laptop locked, swipes do not leave the face."""
    b = at_work
    from conftest import pc_state
    b.state(**pc_state(locked=True))
    b.run(2)
    assert b.page() == 0
    b.swipe_left()
    assert b.page() == 0
    assert b.health()['pc_locked'] is True
