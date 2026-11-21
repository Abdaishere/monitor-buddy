"""The browser installer talking to the firmware itself: connect, read, change, back up, reset."""
import json
import os
import re

import pytest

from conftest import OUT


def settings(buddy):
    """What the firmware says its settings are, asked over USB behind the page's back."""
    return buddy.mb('GET')['config']


def test_connect_shows_the_buddy(desk):
    """Connect: the picker is asked for Espressif boards, the port opens without a reset, one INFO, the Buddy's state."""
    assert desk.title() == desk.text('titles.start')
    desk.connect()
    asked = desk.page.evaluate('window.__mbsim')
    assert asked['filters'] == {'filters': [{'usbVendorId': 0x303a}]}
    assert asked['baud'] == 115200
    assert asked['signals'][:2] == [{'requestToSend': False}, {'dataTerminalReady': False}], 'RTS first, then DTR: no reset'
    assert re.fullmatch(r'@MB INFO \{"id":\d+\}\n', desk.wrote()), 'nothing but one INFO goes to the board'
    page = desk.page.inner_text('main')
    assert desk.text('home.wifiUp', 'HomeWiFi') in page
    assert desk.text('home.linkPaired') in page
    assert not desk.buddy.restarted, 'connecting does not restart the Buddy'
    assert not desk.errors, desk.errors
    desk.shot('home')


def test_picker_cancelled(desk):
    """The port picker is closed without a choice: back on the start screen, without a word."""
    desk.page.evaluate('window.__mbsim.cancel = true')
    desk.click('connect')
    desk.page.wait_for_timeout(500)
    assert desk.title() == desk.text('titles.start')
    assert desk.wrote() == '' and not desk.errors


def test_port_held_by_another_program(desk):
    """Opening fails as if a serial monitor held the port: the page says so and offers to try again."""
    desk.page.evaluate('window.__mbsim.busy = true')
    desk.click('connect')
    desk.wait_title('titles.problem')
    assert desk.text('connecting.portBusy')[:40] in desk.page.inner_text('main')
    desk.page.evaluate('window.__mbsim.busy = false')
    desk.click('problem.retry')
    desk.wait_title('titles.home')


def test_browser_without_web_serial(make_desk):
    """Firefox, Safari, a phone: the page explains instead of offering a button that cannot work."""
    desk = make_desk(serial=False)
    assert desk.text('unsupported') in desk.page.inner_text('main')
    assert desk.page.get_by_role('button').filter(has_text=desk.text('connect')).count() == 0


def test_connect_while_the_buddy_boots(make_desk):
    """Plugged in a moment ago: the page waits through the boot instead of calling the board empty."""
    desk = make_desk(boot_s=0.2)
    desk.click('connect')
    desk.wait_title('titles.home', timeout=40000)
    assert desk.text('home.wifiUp', 'HomeWiFi') in desk.page.inner_text('main')
    assert not desk.errors, desk.errors


def test_change_settings_reach_the_firmware(desk):
    """Change settings: the form shows the board's values; theme, clock and a page changed there are what the firmware runs."""
    before = settings(desk.buddy)
    desk.connect()
    desk.click('home.change')
    desk.wait_title('titles.form')
    themes = desk.text('themeNames')
    checked = desk.page.evaluate("""() => [...document.querySelectorAll('input[type=radio]:checked')].map(r => r.value)""")
    assert str(before['theme']) in checked and before['clock_format'] in checked, 'the form starts from the board'
    new_theme = (before['theme'] + 1) % len(themes)
    new_clock = '12h' if before['clock_format'] == '24h' else '24h'
    desk.page.locator('input[type=radio][value="%d"]' % new_theme).first.check(force=True)
    desk.page.locator('input[type=radio][value="%s"]' % new_clock).check(force=True)
    desk.page.get_by_role('switch', name=desk.text('pageNames.moon'), exact=True).uncheck(force=True)
    desk.shot('form')
    desk.click('form.submit.change')
    desk.wait_title('titles.result', timeout=60000)
    assert desk.text('result.saved') in desk.page.inner_text('main')
    assert desk.buddy.restarted, 'the Buddy restarts with the new settings'
    after = settings(desk.buddy)
    assert after['theme'] == new_theme and after['clock_format'] == new_clock and after['pages']['moon'] is False
    # 'since' (the day the Buddy moved in) is filled in by the page on the first save: meant to change
    changed = ('theme', 'clock_format', 'pages', 'since')
    assert {k: v for k, v in after.items() if k not in changed} == {k: v for k, v in before.items() if k not in changed}, \
        'nothing else moved'
    assert '"wifi"' not in desk.wrote(), 'the Wi-Fi was kept: no password travels'
    assert not desk.errors, desk.errors
    desk.buddy.run(3)
    desk.buddy.shot().save(desk_out('buddy-after-save.png'))


def desk_out(name):
    return os.path.join(OUT, name)


def test_a_wrong_latitude_is_not_sent(desk):
    """A latitude of 123: the form marks it and the board gets no SET."""
    desk.connect()
    desk.click('home.change')
    desk.wait_title('titles.form')
    desk.page.get_by_text(desk.text('form.advanced'), exact=True).first.click()
    desk.page.get_by_label(desk.text('form.lat'), exact=True).fill('123')
    desk.click('form.submit.change')
    desk.page.wait_for_timeout(800)
    assert desk.title() == desk.text('titles.form')
    assert '@MB SET' not in desk.wrote()
    assert desk.page.locator('[aria-invalid=true]').count() >= 1
    assert not desk.buddy.restarted


def test_a_new_wifi_network(make_desk):
    """Set a network: the Buddy restarts, joins it, and the page reports the connection."""
    desk = make_desk(aps=[('HomeWiFi', 'password123', -55), ('CafeNet', 'cafepass99', -60)])
    desk.connect()
    desk.click('home.change')
    desk.wait_title('titles.form')
    desk.page.get_by_text(desk.text('form.wifiNew'), exact=True).click()
    desk.page.get_by_label(desk.text('form.ssid'), exact=True).fill('CafeNet')
    desk.page.get_by_label(desk.text('form.password'), exact=True).fill('cafepass99')
    desk.click('form.submit.change')
    desk.wait_title('titles.result', timeout=90000)
    assert desk.text('result.connected', 'CafeNet') in desk.page.inner_text('main')
    assert desk.buddy.mb('INFO')['wifi'] == {**desk.buddy.mb('INFO')['wifi'], 'ssid': 'CafeNet', 'connected': True}
    assert not desk.errors, desk.errors


def test_a_wrong_wifi_password(make_desk):
    """A password the network refuses: the page says the Buddy could not join. (Its hotspot claim is wrong: xfail.)"""
    desk = make_desk(aps=[('HomeWiFi', 'password123', -55), ('CafeNet', 'cafepass99', -60)])
    desk.connect()
    desk.click('home.change')
    desk.wait_title('titles.form')
    desk.page.get_by_text(desk.text('form.wifiNew'), exact=True).click()
    desk.page.get_by_label(desk.text('form.ssid'), exact=True).fill('CafeNet')
    desk.page.get_by_label(desk.text('form.password'), exact=True).fill('not-the-password')
    desk.click('form.submit.change')
    desk.wait_title('titles.wifiFailed', timeout=150000)
    desk.shot('wifi-failed')
    assert desk.text('result.wifiFailed', 'CafeNet') in desk.page.inner_text('main')
    wifi = desk.buddy.mb('INFO')['wifi']
    assert wifi['ssid'] == 'CafeNet' and not wifi['connected']
    assert not desk.errors, desk.errors
    if not desk.buddy.board()['portal']:
        # BUG (web/js/strings.js result.wifiFailed): the page says "I've opened my setup hotspot", but with a saved
        # network that refuses the password the firmware runs offline ("hold the screen to configure") and opens none.
        pytest.xfail('the page promises a setup hotspot the firmware does not open after a wrong password')


def test_backup_is_the_boards_settings(desk):
    """Back up settings: a JSON file with what the board holds, and never the Wi-Fi password."""
    desk.connect()
    with desk.page.expect_download() as download:
        desk.click('home.backup')
    path = download.value.path()
    name = download.value.suggested_filename
    assert re.fullmatch(r'monitor-buddy-settings-\d{4}-\d\d-\d\d\.json', name)
    saved = json.load(open(path))
    text = open(path).read()
    assert 'password123' not in text
    board = settings(desk.buddy)
    found = saved.get('config', saved)
    assert found['theme'] == board['theme'] and found['lat'] == board['lat'] and found['pages'] == board['pages']
    assert desk.text('backup.saved', name).replace('**', '') in desk.page.inner_text('main')


def test_reset_to_defaults(desk):
    """Reset to defaults: offered a backup first, then asked again; the firmware forgets saved settings, Wi-Fi stays."""
    desk.buddy.mb_restart('SET', {'config': {'theme': 4, 'clock_format': '12h'}})
    desk.buddy.run(12)
    desk.sent = len(desk.buddy.log)
    assert settings(desk.buddy)['theme'] == 4
    desk.connect()
    desk.click('home.reset')
    desk.wait_title('titles.backup')
    desk.click('backup.continue')
    desk.wait_title('titles.reset')
    assert settings(desk.buddy)['theme'] == 4, 'nothing happens before the second yes'
    desk.click('reset.confirm')
    desk.wait_title('titles.result', timeout=60000)
    assert desk.text('result.reset') in desk.page.inner_text('main')
    now = settings(desk.buddy)
    assert now['theme'] != 4 and now['clock_format'] == '24h'
    assert desk.buddy.mb('INFO')['wifi']['connected'], 'the Wi-Fi is kept'


def test_a_board_that_answers_nothing(make_desk):
    """Nothing comes back from the port: the page never hangs; it ends on a screen with a way on."""
    desk = make_desk(open_page=False)
    desk.plugged = False
    desk.open()
    desk.click('connect')
    desk.page.wait_for_function(
        'titles => titles.includes(document.querySelector("h1").innerText)',
        arg=[desk.text('titles.problem'), desk.text('titles.pathB'), desk.text('titles.notBuddy'), desk.text('titles.recover')],
        timeout=90000)
    assert desk.page.get_by_role('button').count() >= 1
    assert '@MB SET' not in desk.wrote() and '@MB DEFAULTS' not in desk.wrote()


def test_firmware_list_enables_installing(make_desk):
    """With a firmware list the install actions are on; without one they are off and the page says why."""
    off = make_desk()
    off.connect()
    update = off.page.get_by_role('button').filter(has_text=off.text('home.updateFw')).first
    assert update.is_disabled()
    on = make_desk(releases=[{'version': '1.0.0', 'cfg_v': 2, 'prerelease': False, 'image': 'firmware/v1.0.0/monitor-buddy-1.0.0.bin',
                              'size': 1, 'sha256': '0' * 64}])
    on.connect()
    assert on.page.get_by_role('button').filter(has_text=on.text('home.updateFw')).first.is_enabled()
    assert on.text('start.manifestError') not in on.page.inner_text('main')


def test_the_form_fits_a_phone(make_desk):
    """At 390 px wide the settings form has no sideways scroll."""
    desk = make_desk(viewport={'width': 390, 'height': 800})
    desk.connect()
    desk.click('home.change')
    desk.wait_title('titles.form')
    wide = desk.page.evaluate('document.documentElement.scrollWidth - document.documentElement.clientWidth')
    assert wide <= 0, 'the page is %d px wider than the phone' % wide
    desk.shot('form-phone')


def test_make_it_yours(desk):
    """You and me: a name for the owner and one for the Buddy, the cat face. The firmware has them after its restart."""
    board = settings(desk.buddy)
    if 'buddy_name' not in board:
        pytest.skip('this firmware has no personal settings')
    desk.connect()
    desk.click('home.change')
    desk.wait_title('titles.form')
    desk.page.get_by_label(desk.text('form.yourName'), exact=True).fill('Sam')
    desk.page.get_by_label(desk.text('form.buddyName'), exact=True).fill('Pixel')
    desk.page.locator('input[type=radio][value="cat"]').check(force=True)
    mark = desk.buddy.mark()
    desk.click('form.submit.change')
    desk.wait_title('titles.result', timeout=60000)
    now = settings(desk.buddy)
    assert (now['name'], now['buddy_name'], now['face']) == ('Sam', 'Pixel', 'cat')
    assert re.fullmatch(r'\d{4}-\d\d-\d\d', now['since']), 'the page notes the day the Buddy moved in'
    assert 'restarting' in desk.buddy.log_since(mark)
    assert not desk.errors, desk.errors
    desk.buddy.run(3)
    desk.buddy.shot().save(desk_out('buddy-cat-face.png'))
