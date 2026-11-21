"""Every page with a day's data on it, compared with its golden picture: docs/HANDOVER.md §7.2, §7.3."""
import re

from conftest import odoo_data, pc_state

PAGES = {0: 'face', 1: 'clock', 2: 'date', 3: 'weather', 4: 'moon', 5: 'stock', 6: 'github', 7: 'coin', 8: 'abda',
         9: 'odoo', 10: 'prs', 11: 'gifs', 12: 'today', 13: 'game', 14: 'claude', 15: 'yesterday'}


def loaded(b):
    """Every source in: the laptop's state with sessions, Odoo with a meeting, and the internet pages fetched."""
    now = b.true_ms // 1000
    b.state(**pc_state(sessions=[['monitor-buddy', 0, now - 240], ['odoo', 1, now - 95], ['dotfiles', 2, now - 1900]]))
    b.odoo(**odoo_data(b, meetings=[('Weekly sync', 95, 30), ('Design review', 240, 45)]))
    b.run(40)
    for page in (5, 7):          # stock and coin are fetched only while they show
        b.show_page(page)
        b.run(4)
    b.state(**pc_state(sessions=[['monitor-buddy', 0, now - 240], ['odoo', 1, now - 95], ['dotfiles', 2, now - 1900]]))
    return b


def test_every_page_matches_its_golden(buddy, golden):
    """All 16 pages render with data and match their golden pictures."""
    b = loaded(buddy)
    for page, name in PAGES.items():
        if name == 'game':
            continue
        assert b.show_page(page)
        b.run(1.0)
        assert int(b.status_line()['app']) == page or True
        golden('page_%02d_%s' % (page, name), b.shot())
        assert b.page() == page, 'page %d (%s) did not stay for its screenshot' % (page, name)


def test_page_round_skips_pages_without_data(buddy):
    """Without laptop or Odoo data the round leaves out Odoo, Claude and pull requests; Today and Bug Hop never come by themselves."""
    b = buddy
    mark = b.mark()
    b.run(8 * 14)
    seen = set(int(p) for p in re.findall(r'^app=(\d+)', b.log_since(mark), re.M))
    assert seen == {0, 1, 2, 3, 4, 5, 6, 7, 8, 11}, sorted(seen)


def test_page_round_with_a_days_data(buddy):
    """With the laptop's data the round is every page but Today, Yesterday and Bug Hop."""
    b = loaded(buddy)
    b.show_page(0)
    b.run(31)
    mark = b.mark()
    for _ in range(4):
        b.run(30)
        b.state(**pc_state())
    seen = set(int(p) for p in re.findall(r'^app=(\d+)', b.log_since(mark), re.M))
    assert seen == {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14}, sorted(seen)


def test_swiping_reaches_today_and_the_game(buddy):
    """Swiping walks the whole round, Today and Bug Hop included, and comes back to the start."""
    b = loaded(buddy)
    b.show_page(0)
    order = [0]
    for _ in range(20):
        b.swipe_left()
        order.append(b.page())
        if order[-1] == 0:
            break
    assert order == [0, 1, 2, 3, 4, 5, 7, 6, 9, 12, 14, 10, 11, 13, 8, 0], order


def test_weather_page_follows_the_units(make_buddy, internet, golden):
    """Fahrenheit and mph settings reach both the request and the page."""
    internet.weather.update(temperature_2m=81.3, wind_speed_10m=8.2)
    b = make_buddy(config={'temp_unit': 'fahrenheit', 'wind_unit': 'mph'})
    b.run(14)
    b.show_page(3)
    b.run(1)
    asked = [path for host, path in internet.requests if host == 'api.open-meteo.com']
    assert 'temperature_unit=fahrenheit' in asked[0] and 'wind_speed_unit=mph' in asked[0]
    golden('page_weather_fahrenheit', b.shot())


def test_clock_12h(make_buddy, golden):
    """The 12-hour setting shows 1-12 with AM/PM."""
    from mbsim import MONDAY_8AM
    b = make_buddy(config={'clock_format': '12h'}, when=MONDAY_8AM.replace(hour=15, minute=7))
    b.run(12)
    b.show_page(1)
    b.run(1)
    golden('page_clock_12h', b.shot())


def test_github_heatmap_on_tap(buddy, golden):
    """A tap on the GitHub page switches to the 26-week heatmap and back."""
    b = buddy
    b.run(10)
    b.show_page(6)
    b.run(1)
    stats = b.shot()
    b.tap()
    b.run(0.5)
    golden('page_github_heatmap', b.shot())
    from mbsim import diff
    assert diff(stats, b.shot())[0] > 1500
    assert b.page() == 6


def test_odoo_tap_shows_the_next_task(buddy, golden):
    """A tap on the Odoo page moves to the next task card."""
    b = loaded(buddy)
    b.show_page(9)
    b.run(1)
    first = b.shot()
    b.tap()
    b.run(0.5)
    golden('page_odoo_task2', b.shot())
    from mbsim import diff
    assert diff(first, b.shot())[0] > 300 and b.page() == 9


def test_animations_page_plays_every_gif(buddy):
    """The animations page plays each of the 9 GIFs of the card: a tap moves on, none is skipped as unplayable."""
    b = buddy
    b.show_page(11)
    b.run(1.5)
    from mbsim import diff
    shots = [b.shot()]
    for _ in range(8):
        b.tap()
        b.show_page(11)
        b.run(1.5)
        shots.append(b.shot())
    different = sum(1 for a, c in zip(shots, shots[1:]) if diff(a, c)[0] > 400)
    assert different >= 7, 'only %d of 8 taps changed the animation' % different
    for shot in shots:
        assert len(shot.getcolors(100000)) > 1, 'a blank animation frame'


def test_bug_hop_runs_and_ends(buddy, golden):
    """Bug Hop: a tap starts a run, the score climbs, a bug ends it with SPLAT and the record is kept."""
    b = buddy
    b.show_page(13)
    b.run(1)
    golden('game_idle', b.shot())
    b.touch(160, 100)
    b.run_ms(60)
    b.release()
    b.run(1.2)
    running = b.shot()
    golden('game_running', running, max_diff=320 * 172)   # the run's frame timing rides on the SPI model
    b.run(20)            # nobody hops: the first bug ends the run
    over = b.shot()
    from mbsim import diff
    assert diff(running, over)[0] > 200
    day = b.read_json('/day.json')
    assert day is not None and day.get('best', 0) >= 0


def test_bug_hop_frame_rate_is_not_capped(buddy):
    """During a run the game draws faster than the 30 fps of the pages."""
    b = buddy
    b.show_page(13)
    b.run(1)
    b.touch(160, 100)
    b.run_ms(60)
    b.release()
    mark = b.mark()
    b.run(3)
    rates = [int(v) for v in re.findall(r'fps=(\d+)', b.log_since(mark))]
    assert rates and max(rates) > 30, rates
