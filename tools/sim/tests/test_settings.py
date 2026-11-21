"""Settings over USB, old settings files, broken ones: docs/HANDOVER.md §6.6, §8."""
import json
import os

from mbsim import ROOT

FIXTURES = os.path.join(ROOT, 'test', 'fixtures')


def test_get_lists_every_setting(buddy):
    """`@MB GET` returns the whole settings, with the config.h defaults on a board without /config.json."""
    answer = buddy.mb('GET')
    assert answer['ok'] and answer['stored'] is False
    config = answer['config']
    assert config['tz_rule'] == '<+04>-4' and config['github_user'] == 'octocat'
    assert config['night'] == {'enabled': True, 'start': 23, 'end': 7, 'brightness': 26}
    assert set(config['pages']) == {'face', 'clock', 'date', 'weather', 'moon', 'stock', 'github', 'coin'}


def test_set_saves_and_restarts(buddy):
    """`@MB SET` writes /config.json, restarts, and the new settings are in force."""
    buddy.mb_restart('SET', {'config': {'pages': {'moon': False}, 'clock_format': '12h', 'theme': 4}})
    buddy.run(12)
    answer = buddy.mb('GET')
    assert answer['stored'] is True
    assert answer['config']['pages']['moon'] is False and answer['config']['clock_format'] == '12h' and answer['config']['theme'] == 4
    assert 'settings: from /config.json' in buddy.log


def test_set_rejects_bad_values_and_changes_nothing(buddy):
    """A SET with one bad value names it, writes nothing and does not restart."""
    answer = buddy.mb('SET', {'config': {'theme': 9, 'lat': '123.4', 'clock_format': '12h'}})
    assert answer['ok'] is False
    assert set(answer['errors']) == {'theme', 'lat'}, answer['errors']
    buddy.run(2)
    assert not buddy.restarted and buddy.read_file('/config.json') is None


def test_set_wifi_validates_the_password(buddy):
    """A Wi-Fi password of 1-7 characters is refused; '' means an open network."""
    assert buddy.mb('SET', {'wifi': {'ssid': 'Cafe', 'password': 'short'}})['ok'] is False
    assert buddy.mb('SET', {'wifi': {'ssid': '', 'password': 'longenough'}})['ok'] is False
    assert buddy.read_json('/wifi.json')['ssid'] == 'HomeWiFi'


def test_defaults_keeps_wifi_and_keys(buddy):
    """`@MB DEFAULTS` deletes /config.json and keeps the Wi-Fi, the brightness and the keys."""
    buddy.write_file('/config.json', json.dumps({'v': 2, 'theme': 0}))
    buddy.write_file('/display.json', json.dumps({'brightness': 99}))
    buddy.mb_restart('DEFAULTS')
    buddy.run(12)
    assert buddy.read_file('/config.json') is None
    assert buddy.read_json('/wifi.json')['ssid'] == 'HomeWiFi'
    assert buddy.read_json('/display.json') == {'brightness': 99}
    info = buddy.mb('INFO')
    assert info['keys']['link'] and info['wifi']['connected']


def test_settings_file_from_firmware_1_2_0_migrates(make_buddy):
    """A /config.json written by firmware 1.2.0 (v1) loads: its values stay, the newer keys take defaults."""
    old = open(os.path.join(FIXTURES, 'config-1.2.0.json')).read()
    b = make_buddy(config=old)
    b.run(12)
    assert 'settings: from /config.json' in b.log
    assert 'unreadable' not in b.log and 'ignored invalid' not in b.log, b.log[:600]
    config = b.mb('GET')
    assert config['config']['tz_rule'] == 'CET-1CEST,M3.5.0,M10.5.0/3'
    assert config['config']['ticker'] == 'AVGO' and config['config']['pages']['moon'] is False
    assert 'night' in config['added'] and 'coins' in config['added'], config['added']
    assert 'NTP synced: 05:00:' in b.log, '08:00 in Dubai is 05:00 in Berlin in January'


def test_settings_file_from_a_future_firmware_loads(make_buddy):
    """A /config.json from a newer firmware (unknown keys) still loads what this one knows."""
    future = open(os.path.join(FIXTURES, 'config-future.json')).read()
    b = make_buddy(config=future)
    b.run(12)
    assert 'settings: from /config.json' in b.log and 'unreadable' not in b.log
    assert b.mb('GET')['config']['ticker'] == 'AVGO'


def test_garbage_config_falls_back_to_defaults(make_buddy):
    """Bad config: an unreadable /config.json is ignored with a log line; the Buddy runs on config.h values."""
    b = make_buddy(config='{"v":2,"theme":')
    b.run(12)
    assert 'settings: /config.json unreadable' in b.log
    assert b.board()['wifi'] and b.mb('GET')['config']['theme'] == 2


def test_wrong_typed_config_values_are_ignored_one_by_one(make_buddy):
    """Bad config: values of the wrong type or range are dropped one by one, the valid ones stay."""
    b = make_buddy(config={'v': 2, 'theme': 'blue', 'brightness': 9999, 'clock_format': '12h', 'lat': 'north',
                           'night': {'enabled': True, 'start': 22, 'end': 22, 'brightness': 3}, 'pages': {'moon': 'no'}})
    b.run(12)
    config = b.mb('GET')['config']
    assert config['clock_format'] == '12h', 'the valid value is kept'
    assert config['theme'] == 2 and config['lat'] == '25.2048'
    assert config['night']['start'] != config['night']['end']
    assert b.log.count('settings: ignored invalid') >= 3, [l for l in b.lines() if 'settings' in l]


def test_empty_and_binary_config_files(make_buddy):
    """Bad config: an empty file and a file of binary rubbish both leave a working Buddy."""
    for content in (b'', bytes(range(256)) * 4, b'null', b'[1,2,3]', b'"text"'):
        b = make_buddy(files={'/config.json': content})
        b.run(12)
        assert b.board()['wifi'], content[:10]
        assert b.mb('GET')['config']['theme'] == 2
        b.close()


def test_right_mount_turns_the_screen(make_buddy, golden):
    """mount: right turns the display 180 degrees (rotation 3) and the touch mapping with it."""
    b = make_buddy(config={'mount': 'right'})
    b.run(12)
    assert b.board()['rotation'] == 3
    b.show_page(1)
    b.run(1)
    golden('page_clock_right_mount', b.shot())
    b.swipe_left()
    assert b.page() == 2, 'a swipe to the left still means next page'


def test_second_wifi_is_used_when_the_first_is_out_of_range(make_buddy):
    """Two saved networks: at boot the one in range becomes /wifi.json and the Buddy joins it."""
    b = make_buddy(aps=[('Office', 'officepass1', -60)],
                   files={'/wifi2.json': json.dumps({'ssid': 'Office', 'password': 'officepass1'})})
    b.run(20)
    assert 'Wi-Fi: using Office' in b.log
    assert b.read_json('/wifi.json')['ssid'] == 'Office' and b.read_json('/wifi2.json')['ssid'] == 'HomeWiFi'
    assert b.board()['ssid'] == 'Office'
    assert b.read_file('/wifi.swap') is None


def test_interrupted_wifi_swap_recovers(make_buddy):
    """A power cut in the middle of a network swap: the next boot puts /wifi.swap back."""
    b = make_buddy(boot=False)
    os.rename(os.path.join(b.data, 'littlefs', 'wifi.json'), os.path.join(b.data, 'littlefs', 'wifi.swap'))
    b.boot()
    b.run(14)
    assert b.read_json('/wifi.json')['ssid'] == 'HomeWiFi' and b.read_file('/wifi.swap') is None
    assert b.board()['wifi']


def test_protocol_errors(buddy):
    """`@MB` errors: unknown command, bad JSON, bad id, a line that is too long."""
    assert buddy.mb('NOPE')['error'] == 'unknown command'
    mark = buddy.mark()
    buddy.serial_write('@MB GET {"id": \n')
    buddy.serial_write('@MB GET {"id": -1}\n')
    buddy.serial_write('@MB GET ' + 'x' * 3200 + '\n')
    buddy.serial_write('plain text is ignored\n')
    buddy.run(1)
    log = buddy.log_since(mark)
    assert '"error":"bad json"' in log and '"error":"bad id"' in log and '"error":"line too long"' in log
    assert buddy.mb('GET', {'id': 77})['id'] == 77
