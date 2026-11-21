"""Power-on: the display comes up, Wi-Fi joins, the clock is set, the same run gives the same pixels."""
import re

from mbsim import Buddy, diff


def test_boot_brings_the_screen_up(make_buddy, golden):
    """Boot: the panel is initialised and lit, HOLD STILL shows while the rest position is measured."""
    b = make_buddy()
    b.run(3)
    board = b.board()
    assert board['panel_on'], 'the init sequence never switched the panel on'
    assert board['backlight'] == 180, 'day brightness before /display.json is read'
    assert board['rotation'] == 1, 'left mount is rotation 1'
    golden('boot_hold_still', b.shot())
    b.run(10)
    assert 'ESP32-C6 Monitor-Buddy' in b.log
    assert 'Touch controller initialised' in b.log
    assert 'knock watch on' in b.log


def test_boot_joins_wifi_and_sets_the_clock(buddy):
    """Boot with a saved network: connected, NTP synced to the right local time, the link listens."""
    assert re.search(r'Wi-Fi connected\. IP: 192\.168\.1\.50', buddy.log)
    assert 'NTP synced: 08:00:' in buddy.log, 'local time must be Dubai, 08:00'
    assert 'link: listening on UDP 4747' in buddy.log
    status = buddy.status_line()
    assert status['wifi'] == 'up' and status['ip'] == '192.168.1.50'
    assert 29 <= int(status['fps']) <= 31
    health = buddy.health()
    assert health['clock_set'] and health['reset'] == 1 and health['slot'] == 'app0'
    assert health['gifs'] == 9 and health['gifs_from'] == 'card'


def test_frame_rate_holds_30(buddy):
    """The loop draws 30 frames a second on every page of the round."""
    buddy.run(60)
    rates = [int(v) for v in re.findall(r'fps=(\d+)', buddy.log)][3:]
    assert rates and min(rates) >= 28, 'slowest second: %d fps' % min(rates)


def test_same_script_same_pixels(internet):
    """Determinism: two boots with the same script show the same screen and print the same log."""
    shots, logs = [], []
    for _ in range(2):
        with Buddy(internet=internet) as b:
            b.run(20)
            b.double_knock()
            b.run(1)
            b.swipe_left()
            b.run(5)
            shots.append(b.shot())
            logs.append(b.log)
    assert diff(shots[0], shots[1])[0] == 0
    assert logs[0] == logs[1]


def test_boot_without_wifi_opens_the_setup_hotspot(make_buddy, golden):
    """First run, no saved network: the setup hotspot opens by itself and shows how to join."""
    b = make_buddy(wifi=None, aps=[('HomeWiFi', 'password123', -50)])
    b.run(8)
    assert b.board()['portal']
    assert 'Captive portal open' in b.log
    golden('portal', b.shot())
    b.set('ap_clients', 1)
    b.run(1)
    golden('portal_one_client', b.shot())


def test_portal_saves_the_network_and_restarts(make_buddy):
    """First run: a phone saves the network on the hotspot's page; the Buddy restarts and joins it."""
    b = make_buddy(wifi=None, aps=[('HomeWiFi', 'password123', -50)])
    b.run(8)
    b.portal_save('HomeWiFi', 'password123')
    assert b.restarted and b.reset_reason == 3
    b.run(12)
    assert b.read_json('/wifi.json') == {'ssid': 'HomeWiFi', 'password': 'password123'}
    assert not b.board()['portal'] and b.board()['wifi']
    assert b.mb('INFO')['wifi'] == {'saved': True, 'ssid': 'HomeWiFi', 'connected': True, 'rssi': -50}


def test_portal_closes_after_five_idle_minutes(make_buddy):
    """The setup hotspot closes by itself after 300 s with no phone on it."""
    b = make_buddy(wifi=None)
    b.run(8)
    b.set('ap_clients', 1)
    b.run(400)
    assert b.board()['portal'], 'a joined phone keeps it open'
    b.set('ap_clients', 0)
    b.run(290)
    assert b.board()['portal']
    b.run(20)
    assert not b.board()['portal']


def test_hold_opens_and_closes_the_hotspot(buddy):
    """Holding a finger still for 2.5 s opens the setup hotspot; lifting and holding again closes it."""
    buddy.hold(3)
    assert buddy.board()['portal']
    assert 'Opening Wi-Fi setup portal' in buddy.log
    buddy.run(2)
    buddy.hold(3)
    assert not buddy.board()['portal']
    buddy.run(8)
    assert buddy.board()['wifi'], 'it reconnects to the saved network'


def test_first_run_keys_over_usb(make_buddy):
    """First run: without keys the link is shut; `@MB KEYS` stores them, restarts, and the link answers."""
    from mbsim import LINK_KEY, UPDATE_KEY_PEM
    b = make_buddy(keys=False)
    b.run(12)
    assert b.mb('INFO')['keys'] == {'link': False, 'update': False}
    assert b.ping() is None, 'no link key: no answer'
    assert b.mb('KEYS', {'link_key': 'abc'})['errors'] == {'link_key': 'must be 64 hex digits'}
    assert b.mb('KEYS', {'update_key': 'not a key'})['ok'] is False
    assert b.mb_restart('KEYS', {'link_key': LINK_KEY, 'update_key': UPDATE_KEY_PEM})['ok']
    b.run(12)
    info = b.mb('INFO')
    assert info['keys']['link'] and info['keys']['update'], info
    assert LINK_KEY not in b.log and 'BEGIN PUBLIC KEY' not in b.log.split('@MB KEYS')[-1], 'the keys are never printed'
    assert b.ping() is not None
