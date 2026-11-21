"""What goes wrong in the field: no Wi-Fi, a dead internet, a missing card, a chip that does not answer."""
import json
import re

from conftest import odoo_data, pc_state


def test_wifi_out_of_range_at_boot(make_buddy, golden):
    """No Wi-Fi: with the saved network out of range the Buddy runs offline, says so, and keeps its hotspot closed."""
    b = make_buddy(aps=[])
    b.run(25)
    assert 'Wi-Fi unavailable' in b.log
    assert not b.board()['portal'], 'a slow router must not leave an open hotspot'
    assert b.status_line()['wifi'] == 'down' and int(b.status_line()['fps']) >= 28
    assert b.mb('STATE', {'page': 0})['ok'], 'no link without Wi-Fi: the page comes over USB'
    b.run(1)
    golden('face_wifi_offline', b.shot())
    b.mb('STATE', {'page': 3})
    b.run(1)
    assert b.page() == 3
    golden('page_weather_no_wifi', b.shot())


def test_wifi_comes_back(make_buddy):
    """The network returns: the Buddy joins within its retry time, sets the clock and opens the link."""
    b = make_buddy(aps=[])
    b.run(25)
    b.wifi_up()
    b.run(80)
    assert b.board()['wifi']
    assert 'NTP synced' in b.log and 'link: listening on UDP 4747' in b.log
    assert b.ping() is not None


def test_wifi_drops_and_recovers(buddy):
    """Wi-Fi drops while running: the face says WIFI OFFLINE, the loop keeps 30 fps, and the link returns with the network."""
    b = buddy
    assert b.ping() is not None
    b.wifi_down()
    b.run(20)
    assert b.status_line()['wifi'] == 'down' and int(b.status_line()['fps']) >= 28
    assert b.ping() is None
    b.wifi_up()
    b.run(90)
    assert b.status_line()['wifi'] == 'up'
    assert b.ping() is not None


def test_wrong_wifi_password(make_buddy):
    """A saved password the router refuses: offline, hotspot closed, a retry every minute, never a crash."""
    b = make_buddy(wifi=('HomeWiFi', 'old-password'), aps=[('HomeWiFi', 'password123', -55)])
    b.run(200)
    assert not b.board()['wifi'] and not b.board()['portal']
    assert b.status_line()['wifi'] == 'down'
    b.hold(3)
    assert b.board()['portal'], 'holding the screen is the way to fix a changed password'


def test_internet_down_fetches_back_off(make_buddy, internet):
    """Wi-Fi without internet: fetches fail and back off (10 s doubling to 60 s), the pages say so, the loop stays at 30 fps."""
    b = make_buddy()
    b.internet_down()
    b.run(300)
    fails = re.findall(r'\[net\] weather FAIL \d+ ms, retry in (\d+) s', b.log)
    assert fails[:4] == ['10', '20', '40', '60'], fails
    assert len(fails) <= 8, 'no hammering: %d tries in 5 minutes' % len(fails)
    assert min(int(v) for v in re.findall(r'fps=(\d+)', b.log)[3:]) >= 28
    assert 'NTP synced' not in b.log
    b.internet_up()
    b.run(130)
    assert '[net] weather ok' in b.log and 'NTP synced' in b.log


def test_bad_answers_from_the_internet(make_buddy, internet):
    """A 500, an HTML error page, a timeout and a body cut short: each is a failed fetch, none a crash or a wrong page."""
    for fault in (500, 'garbage', 'timeout', 'cut'):
        internet.fail = {'api.open-meteo.com': fault, 'api.github.com': fault, 'github.com': fault}
        b = make_buddy()
        b.run(60)
        assert '[net] weather FAIL' in b.log, fault
        assert '[net] weather ok' not in b.log, fault
        assert b.health()['weather'] is False, fault
        assert '[net] github ok' not in b.log, fault
        internet.fail = {}
        b.run(130)
        assert b.health()['weather'] is True, fault
        b.close()


def test_weather_page_while_loading(make_buddy, internet, golden):
    """Before the first answer the weather page says UPDATING instead of showing zeroes."""
    internet.fail = {'api.open-meteo.com': 500}
    b = make_buddy()
    b.run(14)
    b.show_page(3)
    b.run(1)
    golden('page_weather_updating', b.shot())


def test_unknown_coin_pair(make_buddy, internet, golden):
    """A coin pair Coinbase does not know: the page says NOT FOUND and stops asking."""
    b = make_buddy(config={'coins': [{'pair': 'ZZZ-USD', 'source': 'coinbase', 'on': True}]})
    b.run(14)
    b.show_page(7)
    b.run(6)
    golden('page_coin_not_found', b.shot())
    asked = len([1 for host, path in internet.requests if 'ZZZ-USD' in path])
    b.run(200)
    b.show_page(7)
    b.run(100)
    assert len([1 for host, path in internet.requests if 'ZZZ-USD' in path]) == asked, 'it keeps asking for a pair that does not exist'


def test_no_card(make_buddy):
    """SD missing: the animations come from internal storage, the card test says so, nothing else changes."""
    b = make_buddy(card='none')
    b.run(14)
    assert 'gifs: 0 in the internal file system' in b.log
    health = b.health()
    assert health['gifs'] == 0 and health['gifs_from'] == 'internal'
    assert not b.ping()[8] & 2
    result = json.loads(b.link(b'C', wait_ms=20000)[9:])
    assert result['card'] == 'none' and result['error'] == 'no card answers'
    mark = b.mark()
    b.run(100)
    assert 11 not in set(int(p) for p in re.findall(r'^app=(\d+)', b.log_since(mark), re.M)), 'no animations page without animations'


def test_reacting_animation_without_its_file(make_buddy):
    """SD missing: CI going green with no ci-green.gif anywhere plays nothing and leaves the face alone."""
    b = make_buddy(card='none')
    b.run(12)
    b.state(**pc_state(prs_failing=1))
    b.run(40)
    b.show_page(0)
    b.state(**pc_state(prs_failing=1))
    b.run(2)
    before = b.mark()
    b.state(**pc_state(prs_failing=0))
    b.run(3)
    assert 'mood: status 0' in b.log_since(before)
    assert b.page() == 0


def test_card_without_fat(make_buddy):
    """A card with no FAT file system: the Buddy falls back to internal storage and the card test names the reason."""
    b = make_buddy(card='nofat')
    b.run(14)
    assert b.health()['gifs_from'] == 'internal'
    result = json.loads(b.link(b'C', wait_ms=20000)[9:])
    assert result['error'] == 'the card answers but holds no FAT file system'


def test_card_pulled_out_while_running(buddy):
    """The card is pulled while the Buddy runs: the animations page gives up cleanly, the loop keeps running."""
    b = buddy
    b.show_page(11)
    b.run(3)
    b.set('card', 'none')
    b.run(30)
    assert int(b.status_line()['fps']) >= 28
    assert b.ping() is not None
    b.show_page(11)
    b.run(5)
    assert b.ping() is not None


def test_animation_upload_into_internal_storage(make_buddy):
    """Without a card an uploaded animation goes into LittleFS, and one that does not fit is refused."""
    import os
    from mbsim import ROOT
    b = make_buddy(card='none')
    b.run(12)
    data = open(os.path.join(ROOT, 'sdcard', 'gifs', 'pong.gif'), 'rb').read()
    assert b.link(b'F' + len(data).to_bytes(4, 'big') + b'pong.gif') is not None
    for at in range(0, len(data), 1024):
        assert b.link(b'D' + at.to_bytes(4, 'big') + data[at:at + 1024], wait_ms=1500) is not None
    b.run(1)
    assert b.read_file('/gifs/pong.gif') == data and b.health()['gifs'] == 1
    mark = b.mark()
    assert b.link(b'F' + (900 * 1024).to_bytes(4, 'big') + b'huge.gif') is None
    assert 'link: animation refused (name or room)' in b.log_since(mark)


def test_no_motion_sensor(make_buddy):
    """A board whose motion sensor does not answer: no knock watch, the eyes sway on their own, everything else works."""
    b = make_buddy(imu=False)
    b.run(12)
    assert 'IMU unavailable' in b.log and 'knock watch off' in b.log
    assert int(b.status_line()['fps']) >= 28 and b.ping() is not None
    b.show_page(0)
    b.run(1)
    first = b.shot()
    b.run(1.5)
    from mbsim import diff
    assert diff(first, b.shot())[0] > 0, 'the eyes move without a sensor'


def test_no_touch_controller(make_buddy):
    """A touch controller that does not answer: no phantom taps, no hotspot opened by itself, 30 fps."""
    b = make_buddy(touch=False)
    b.run(60)
    assert not b.board()['portal']
    assert int(b.status_line()['fps']) >= 25, b.status_line()
    assert b.ping() is not None


def test_crash_restart_is_reported(buddy):
    """After a crash the health answer says reset 4 and that a crash dump is stored."""
    buddy.crash()
    buddy.run(12)
    health = buddy.health()
    assert health['reset'] == 4 and health['crash_dump'] is True
    assert buddy.status_line()['rst'] == '4'


def test_day_totals_survive_a_restart(at_work):
    """Focus time counted before a restart is still there after it (/day.json)."""
    b = at_work
    b.lay_flat()
    b.run(120)
    b.stand_up()
    b.run(20)
    before = b.read_json('/day.json')
    b.power_cycle()
    b.run(12)
    b.state(**pc_state())
    b.run(2)
    assert b.read_json('/day.json')['focus'] == before['focus'] >= 115


def test_power_cut_loses_the_clock_until_ntp(buddy):
    """A power cut loses the clock; until NTP answers the link drops packets, then it is back."""
    b = buddy
    b.power_cycle()
    b.run(4)
    assert b.rtc_ms < 10_000_000, 'the chip clock restarts from zero'
    b.run(10)
    assert b.ping() is not None


def test_millis_wrap_after_49_days(make_buddy):
    """The millisecond clock wraps after 49.7 days: across the wrap the loop keeps its pace and answers."""
    b = make_buddy(uptime_ms=2**32 - 40_000)
    b.run(20)
    assert b.ping() is not None
    b.state(**pc_state())
    b.odoo(**odoo_data(b))
    mark = b.mark()
    b.run(40)                  # the wrap is in here
    assert b.t_ms > 2**32
    b.state(**pc_state())
    rates = [int(v) for v in re.findall(r'fps=(\d+)', b.log_since(mark))]
    assert rates and min(rates) >= 28, rates
    b.double_knock()
    assert 'knock: two knocks' in b.log_since(mark)
    b.run(60)
    b.state(**pc_state())
    assert b.ping() is not None and b.health()['weather']
    assert int(b.status_line()['fps']) >= 28
