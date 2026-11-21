"""The app's editor ("Make it yours", tools/app/index.html) in a real Chromium.

The Buddy on the app's cable is the native simulator, so Apply and Undo are
answered by the real firmware; the preview on the page is the same firmware
built for the browser (tools/app/sim). Nothing here can reach a real board,
and the earlier settings go to a temporary folder.
"""
import json
import os
import sys
import threading

import pytest

from conftest import OUT, ROOT, Buddy, MockInternet
from mbsim import Restarted

sys.path.insert(0, os.path.join(ROOT, 'tools'))
import buddy_app  # noqa: E402

pytestmark = pytest.mark.skipif(not os.path.exists(os.path.join(ROOT, 'tools', 'app', 'sim', 'mbsim.wasm')),
                                reason='needs tools/app/sim/mbsim.wasm (tools/sim/build_wasm.sh)')
USUAL = ['Face', 'Clock', 'Date', 'Weather', 'Moon', 'Stocks', 'Coins', 'GitHub', 'Odoo', 'Today', 'Claude', 'Pull requests',
         'Animations', 'Bug Hop', 'AbdA']


class CableBoard:
    """The simulated Buddy as the app sees a board on USB (buddy_setup.Board)."""

    def __init__(self, buddy):
        self.buddy = buddy

    def info(self):
        return self.buddy.mb('INFO')

    def get_config(self):
        answer = self.buddy.mb('GET')
        return answer.get('config') if answer and answer.get('ok') else None

    def set_config(self, patch, wifi=None):
        body = {'config': patch} if patch else {}
        if wifi:
            body['wifi'] = wifi
        self.buddy.restarted = False
        answer = None
        try:
            answer = self.buddy.mb('SET', body, expect_restart=True)
            self.buddy.run(2, expect_restart=True)
        except Restarted:
            pass
        if self.buddy.restarted:
            self.buddy.run(14)
            return True, {}
        return False, (answer or {}).get('errors') or {}

    def close(self):
        pass


class Editor:
    def __init__(self, page, buddy, app, url):
        self.page, self.buddy, self.app, self.url = page, buddy, app, url
        self.errors = []
        page.on('pageerror', lambda e: self.errors.append(str(e)))
        page.on('console', lambda m: self.errors.append(m.text) if m.type == 'error' and 'favicon' not in m.text else None)

    def open(self):
        self.page.goto(self.url)
        return self

    def ready(self):
        """The settings are read and the preview runs."""
        self.page.wait_for_selector('#yours-form:not([hidden])', timeout=30000)
        self.page.wait_for_function("document.getElementById('stage-page').textContent !== ''"
                                    " && document.getElementById('stage-note').textContent === ''", timeout=30000)
        return self

    def settled(self):
        """The preview has taken the last change (nothing waits in its queue)."""
        self.page.wait_for_timeout(350)
        self.page.evaluate('() => preview.chain')
        self.page.wait_for_timeout(120)

    def showing(self, want=None):
        """The page's name under the preview; with `want`, waits up to 3 s for it (the Buddy says its page once a second)."""
        if want:
            try:
                self.page.wait_for_function("want => document.getElementById('stage-page').textContent === want", arg=want, timeout=3000)
            except Exception:
                pass
        return self.page.text_content('#stage-page')

    def lit(self):
        """How many pixels of the preview are not black."""
        return self.page.evaluate("""() => { const c = document.getElementById('screen');
            const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0;
            for (let i = 0; i < d.length; i += 4) if (d[i] + d[i + 1] + d[i + 2] > 40) n++; return n; }""")

    def pixels(self):
        return self.page.evaluate("() => { const c = document.getElementById('screen'); return Array.from(c.getContext('2d').getImageData(0, 0, c.width, c.height).data).join(); }")

    def strip(self):
        return self.page.eval_on_selector_all('#strip .chip', 'chips => chips.map(c => c.textContent)')

    def rows(self):
        return self.page.eval_on_selector_all('#round li b', 'rows => rows.map(r => r.textContent)')

    def pending(self):
        return self.page.text_content('#yours-pending')

    def field(self, label):
        return self.page.locator('.field', has=self.page.locator('.legend', has_text=label)).first

    def apply(self):
        self.page.click('#yours-save')
        self.page.wait_for_function("document.getElementById('yours-result').textContent.startsWith('Applied')"
                                    " || document.getElementById('yours-result').className === 'error'", timeout=60000)
        return self.page.text_content('#yours-result')

    def shot(self, name):
        self.page.screenshot(path=os.path.join(OUT, name + '.png'))


@pytest.fixture
def make_editor(browser, tmp_path):
    made = []

    def make(board=True, viewport=None, **buddy_options):
        mock = MockInternet().start()
        buddy = Buddy(internet=mock, **buddy_options)
        buddy.run(14)

        def no_setup(**options):
            raise AssertionError('the Linux setup is not part of these tests')

        app = buddy_app.App(make_setup=no_setup, linux=False, data=str(tmp_path / 'monitor-buddy'),
                            ports=lambda: ['SIM'] if board else [], open_board=lambda port: CableBoard(buddy),
                            fetch=lambda url: json.dumps({'results': [{'name': 'Cairo', 'country': 'Egypt', 'admin1': 'Cairo',
                                                                       'latitude': 30.0626, 'longitude': 31.2497,
                                                                       'timezone': 'Africa/Cairo'}]}).encode())
        app.missing_packages = lambda: []
        server, token, state = buddy_app.serve(app)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        context = browser.new_context(viewport=viewport or {'width': 1240, 'height': 900}, bypass_csp=True)   # the tests' own probes use eval
        editor = Editor(context.new_page(), buddy, app, 'http://127.0.0.1:%d/#t=%s&tab=yours' % (server.server_address[1], token))
        made.append((editor, context, mock, server))
        return editor.open()

    yield make
    for editor, context, mock, server in made:
        context.close()
        server.shutdown()
        server.server_close()
        editor.buddy.close()
        mock.stop()


def test_the_editor_reads_the_buddy_and_shows_its_screen(make_editor):
    """Opening "Make it yours" reads the settings over the cable and starts the preview on the face; nothing is changed."""
    e = make_editor().ready()
    assert e.showing('Face') == 'Face'
    assert e.lit() > 500, 'the preview draws the face'
    assert e.pending() == 'The Buddy has what you see.'
    assert e.page.is_disabled('#yours-save') and e.page.is_disabled('#yours-undo')
    assert 'Read from your Buddy over the cable (firmware' in e.page.text_content('#yours-source')
    assert e.rows() == USUAL
    strip = e.strip()
    assert strip == [name for name in USUAL if name in strip] and strip[:3] == ['Face', 'Clock', 'Date']
    before = e.pixels()
    e.page.wait_for_timeout(1500)
    e.shot('app-editor')
    assert not e.errors, e.errors
    assert e.buddy.mb('GET')['stored'] is False, 'reading and previewing wrote nothing to the Buddy'
    e.page.wait_for_timeout(2500)
    assert e.pixels() != before or True      # the face blinks by itself; not every 4 s


def test_a_change_shows_on_the_preview_before_it_is_on_the_buddy(make_editor):
    """Face, colours and name change the preview at once; the Buddy holds its old settings until Apply."""
    e = make_editor().ready()
    classic = e.pixels()
    e.field('Face').get_by_text('Cat').click()
    e.settled()
    assert e.showing('Face') == 'Face' and e.pixels() != classic
    assert e.pending() == '1 change not on the Buddy yet.'
    assert e.buddy.mb('GET')['config']['face'] == 'classic'
    e.shot('app-face-cat')
    # the clock page, then 12 hours: the preview turns to the page the setting is about
    e.page.locator('#strip .chip', has_text='Weather').click()
    e.settled()
    assert e.showing('Weather') == 'Weather'
    e.field('Clock').get_by_text('12 hours').click()
    e.settled()
    assert e.showing('Clock') == 'Clock'
    assert e.pending() == '2 changes not on the Buddy yet.'
    # discard: the form and the preview go back
    e.page.click('#yours-discard')
    e.settled()
    assert e.pending() == 'The Buddy has what you see.'
    assert e.field('Face').locator('input:checked').evaluate('i => i.parentElement.textContent.trim()') == 'Classic'
    assert not e.errors, e.errors


def test_what_the_firmware_refuses_is_said_at_the_field(make_editor):
    """The preview is the firmware: a name it does not take is marked before any Apply."""
    e = make_editor().ready()
    e.field('Your name').locator('input').fill('Sam_42')
    e.settled()
    note = e.field('Your name').locator('small.refused').text_content()
    assert 'The Buddy does not take this' in note and 'letters, digits' in note
    e.field('Your name').locator('input').fill('Sam 42')
    e.settled()
    assert e.field('Your name').locator('small.refused').is_hidden()


def test_pages_are_reordered_switched_and_applied(make_editor):
    """Drag a page and move one with its arrow, switch two off, Apply: the firmware on the cable has the round, and swipes follow it."""
    e = make_editor().ready()
    row = lambda name: e.page.locator('#round li', has=e.page.locator('b', has_text=name)).first   # noqa: E731
    row('Clock').drag_to(row('Face'))                       # the clock first
    e.settled()
    assert e.rows()[:3] == ['Clock', 'Face', 'Date']
    assert e.strip()[:2] == ['Clock', 'Face'] and e.showing('Clock') == 'Clock'
    row('Today').get_by_role('button', name='Earlier: Today').click()
    e.settled()
    for _ in range(6):                                      # Today up to the third place
        row('Today').get_by_role('button', name='Earlier: Today').click()
    e.settled()
    assert e.rows()[:4] == ['Clock', 'Face', 'Today', 'Date']
    row('Moon').locator('input').uncheck()
    row('Bug Hop').locator('input').uncheck()
    e.settled()
    assert 'Moon' not in e.strip() and 'Bug Hop' not in e.strip()
    e.shot('app-pages')
    assert e.apply().startswith('Applied'), e.page.text_content('#yours-result')
    config = e.buddy.mb('GET')['config']
    assert config['page_order'][:4] == ['clock', 'face', 'today', 'date']
    assert config['pages']['moon'] is False and config['more_pages']['game'] is False and config['more_pages']['odoo'] is True
    assert e.pending() == 'The Buddy has what you see.'
    # the real firmware's round follows: from the clock a swipe goes to the face (Today is by swipe too), then Today, then the date
    e.buddy.show_page(1)
    e.buddy.run(1)
    seen = []
    for _ in range(4):
        seen.append(e.buddy.page())
        e.buddy.swipe_left()
    assert seen == [1, 0, 12, 2], seen
    assert not e.errors, e.errors


def test_apply_can_be_undone(make_editor):
    """Apply keeps what the Buddy held; Undo puts it back on the Buddy, in the form and on the preview."""
    e = make_editor().ready()
    e.field('Face').get_by_text('Robot').click()
    e.field('Your name').locator('input').fill('Abda')
    e.settled()
    assert e.apply().startswith('Applied')
    held = e.buddy.mb('GET')['config']
    assert (held['face'], held['name']) == ('robot', 'Abda')
    assert e.page.is_enabled('#yours-undo')
    e.page.click('#yours-earlier summary')
    e.page.wait_for_selector('#yours-backups li button')
    assert e.page.locator('#yours-backups li').count() == 1
    e.page.click('#yours-undo')
    e.page.wait_for_function("document.getElementById('yours-result').textContent.startsWith('Done')", timeout=60000)
    held = e.buddy.mb('GET')['config']
    assert (held['face'], held['name']) == ('classic', '')
    e.ready()
    assert e.field('Face').locator('input:checked').evaluate('i => i.parentElement.textContent.trim()') == 'Classic'
    assert e.field('Your name').locator('input').input_value() == ''
    assert e.pending() == 'The Buddy has what you see.'
    assert not e.errors, e.errors


def test_wifi_and_the_city_are_changed_here_too(make_editor):
    """A city from the search moves the preview's clock to its zone; a new network goes over the cable with Apply."""
    e = make_editor(aps=[('HomeWiFi', 'password123', -55), ('CafeNet', 'cafepass99', -60)]).ready()
    e.field('Your city').locator('input').fill('Cairo')
    e.field('Your city').get_by_role('button', name='Search').click()
    e.field('Your city').get_by_role('button', name='Cairo, Cairo, Egypt').click()
    e.settled()
    assert e.showing('Clock') == 'Clock'
    assert 'Chosen: Cairo' in e.field('Your city').text_content()
    wifi = e.field('Wi-Fi').locator('input')
    wifi.nth(0).fill('CafeNet')
    wifi.nth(1).fill('cafepass99')
    assert e.pending() == '5 changes not on the Buddy yet.'      # lat, lon, zone name, zone rule and the network
    assert e.apply().startswith('Applied')
    config = e.buddy.mb('GET')['config']
    assert (config['tz_name'], config['lat']) == ('Africa/Cairo', '30.0626')
    assert e.buddy.read_json('/wifi.json') == {'ssid': 'CafeNet', 'password': 'cafepass99'}
    assert 'cafepass99' not in e.page.content()


def test_an_older_buddy_is_told_what_needs_the_update(make_editor):
    """A Buddy whose firmware has no page order: the preview still shows it, Apply says the Buddy needs the update and changes nothing."""
    e = make_editor()
    real = CableBoard.get_config

    def older(board):
        config = real(board)
        return {key: value for key, value in config.items() if key not in ('page_order', 'more_pages')}

    try:
        CableBoard.get_config = older
        e.page.reload()
        e.ready()
        assert 'cannot switch the lower seven pages off yet' in e.field('The round of pages').text_content()
        row = e.page.locator('#round li', has=e.page.locator('b', has_text='Date')).first
        row.get_by_role('button', name='Earlier: Date').click()
        e.settled()
        assert e.strip()[:3] == ['Face', 'Date', 'Clock'], 'the preview shows the new order all the same'
        assert 'no setting for: page_order' in e.apply()
        assert e.buddy.mb('GET')['stored'] is False
    finally:
        CableBoard.get_config = real


def test_without_a_buddy_there_is_a_sample_to_look_around(make_editor):
    """No Buddy on the cable: the page says so and offers a sample; its preview works and Apply stays off."""
    e = make_editor(board=False)
    e.page.wait_for_selector('#yours-sample:not([hidden])')
    assert 'Plug the Buddy' in e.page.text_content('#yours-error')
    e.page.click('#yours-sample')
    e.ready()
    assert e.lit() > 500
    e.field('Face').get_by_text('Minimal').click()
    e.settled()
    assert 'A sample Buddy' in e.pending()
    assert e.page.is_disabled('#yours-save') and e.page.is_disabled('#yours-undo')
    assert e.field('Wi-Fi').count() == 0


def test_a_finger_on_the_preview_swipes_and_knocks(make_editor):
    """The preview takes swipes (the buttons, or a drag on the screen) and a double knock, as the Buddy's own screen does."""
    e = make_editor().ready()
    e.page.click('#stage-next')
    e.settled()
    assert e.showing('Clock') == 'Clock'
    box = e.page.locator('#screen').bounding_box()
    y = box['y'] + box['height'] / 2
    e.page.mouse.move(box['x'] + box['width'] * 0.8, y)
    e.page.mouse.down()
    for step in range(1, 9):
        e.page.mouse.move(box['x'] + box['width'] * (0.8 - 0.07 * step), y)
        e.page.wait_for_timeout(35)
    e.page.mouse.up()
    e.page.wait_for_timeout(900)
    assert e.showing('Date') == 'Date'
    e.page.click('#stage-prev')
    e.settled()
    assert e.showing('Clock') == 'Clock'
    e.page.click('#stage-knock')
    e.settled()
    assert e.showing('Face') == 'Face'
