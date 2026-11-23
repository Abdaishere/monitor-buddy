"""The Wi-Fi link: sealing, replay and clock checks, every message: docs/HANDOVER.md §6.1, §6.2."""
import json
import os

from mbsim import HEIGHT, ROOT, diff


def test_ping_answers_with_build_and_status(buddy):
    """`P`: the answer carries the 8-byte build id and the status byte (animations on the card)."""
    answer = buddy.ping()
    assert answer is not None and len(answer) == 9
    assert answer[8] & 2, 'bit 1: the animations come from the card'


def test_wrong_key_gets_no_answer(buddy):
    """A packet sealed with another key is ignored without a word."""
    mark = buddy.mark()
    assert buddy.link(b'P', key=bytes(32)) is None
    assert 'link:' not in buddy.log_since(mark)


def test_tampered_packet_is_ignored(buddy):
    """One flipped bit in a sealed packet: no answer."""
    _, packet = buddy.seal(b'P')
    broken = packet[:14] + bytes([packet[14] ^ 1]) + packet[15:]
    assert buddy.link_raw(broken) == []
    assert buddy.link_raw(b'short') == []
    assert buddy.link_raw(os.urandom(1500)) == []
    assert buddy.ping() is not None, 'the link still works after the rubbish'


def test_replayed_packet_is_dropped(buddy):
    """The same packet twice: the second is dropped as old."""
    _, packet = buddy.seal(json.dumps({'locked': True}).encode())
    assert len(buddy.link_raw(packet)) == 1
    mark = buddy.mark()
    assert buddy.link_raw(packet) == []
    assert 'link: old packet dropped' in buddy.log_since(mark)


def test_packet_from_the_wrong_time_is_dropped(buddy):
    """A stamp more than 60 s off the Buddy's clock is dropped; 30 s off is taken."""
    assert buddy.link(b'P', stamp_ms=buddy.true_ms + 30_000) is not None
    assert buddy.link(b'P', stamp_ms=buddy.true_ms + 120_000) is None
    assert buddy.link(b'P', stamp_ms=buddy.true_ms - 120_000) is None


def test_link_is_shut_before_the_clock_is_set(make_buddy):
    """With no internet the clock is never set, so link packets are dropped as out of time."""
    b = make_buddy(internet=None)
    b.run(15)
    assert 'link: listening on UDP 4747' in b.log
    assert b.ping() is None
    assert 'link: old packet dropped' in b.log_since(0)


def test_show_page_holds_for_30_seconds(buddy):
    """`G n` shows page n and holds the automatic flipping for 30 s."""
    assert buddy.show_page(4)
    buddy.run(25)
    assert buddy.page() == 4
    buddy.run(10)
    assert buddy.page() != 4
    assert buddy.link(b'G' + bytes([99])) is not None, 'an unknown page is answered and ignored'


def test_screen_rows_over_the_link_match_the_panel(buddy):
    """`R n` returns the screen's rows: a screenshot over the link is the picture on the LCD."""
    buddy.show_page(2)
    buddy.run(1.2)
    over_link = buddy.link_shot()
    assert over_link is not None
    assert diff(over_link, buddy.shot())[0] == 0
    assert buddy.link(b'R' + bytes([HEIGHT])) is None, 'a row past the end gets no answer'


def test_usb_screenshot_matches_the_panel(buddy):
    """`@MB SHOT` over USB gives the same picture as the LCD."""
    buddy.show_page(2)
    buddy.run(1.2)
    shot = buddy.usb_shot()
    assert shot is not None
    assert diff(shot, buddy.shot())[0] == 0


def test_health_answer(buddy):
    """`H`: every field of the health answer, with sane values."""
    buddy.run(20)
    health = buddy.health()
    assert set(health) >= {'up_s', 'reset', 'heap', 'heap_min', 'rssi', 'clock_set', 'slot', 'fs_kb', 'fs_total_kb', 'gifs',
                           'gifs_from', 'odoo_age_s', 'weather', 'github', 'pc_locked', 'crash_dump', 'double_knocks', 'last_knocks_g'}
    assert 30 <= health['up_s'] <= 34 and health['rssi'] == -55 and health['fs_total_kb'] == 896
    assert health['weather'] and health['github'] and health['odoo_age_s'] == -1 and not health['crash_dump']


def test_upload_an_animation(buddy):
    """`F` then `D` pieces store an animation; it is counted, playable and removable with `X`."""
    data = open(os.path.join(ROOT, 'sdcard', 'gifs', 'pong.gif'), 'rb').read()
    name = b'sim-test.gif'
    assert buddy.link(b'F' + len(data).to_bytes(4, 'big') + name) is not None
    for at in range(0, len(data), 1024):
        assert buddy.link(b'D' + at.to_bytes(4, 'big') + data[at:at + 1024], wait_ms=1500) is not None, 'piece at %d' % at
    buddy.run(1)
    assert buddy.read_file('/gifs/sim-test.gif', fs='sd') == data
    assert buddy.read_file('/gifs/sim-test.gif.part', fs='sd') is None
    assert buddy.health()['gifs'] == 10
    assert buddy.link(b'X' + name) is not None
    assert buddy.health()['gifs'] == 9


def test_upload_with_a_bad_name_is_refused(buddy):
    """An animation named outside letters, digits, dot, dash and underscore, or not .gif, is refused."""
    mark = buddy.mark()
    for name in (b'../config.gif', b'.hidden.gif', b'a.png', b'x' * 60 + b'.gif', b'sp ace.gif'):
        assert buddy.link(b'F' + (1000).to_bytes(4, 'big') + name) is None, name
    assert buddy.log_since(mark).count('link: animation refused') == 5
    assert buddy.files('sd') == sorted('gifs/' + n for n in os.listdir(os.path.join(ROOT, 'sdcard', 'gifs')))


def test_broken_off_upload_leaves_no_part_file(buddy):
    """An animation upload that stops half way: after 10 s the part file is gone and the Buddy is back."""
    data = open(os.path.join(ROOT, 'sdcard', 'gifs', 'pong.gif'), 'rb').read()
    buddy.link(b'F' + len(data).to_bytes(4, 'big') + b'half.gif')
    buddy.link(b'D' + (0).to_bytes(4, 'big') + data[:1024], wait_ms=1500)
    buddy.run(12)
    assert buddy.read_file('/gifs/half.gif.part', fs='sd') is None and buddy.read_file('/gifs/half.gif', fs='sd') is None
    assert buddy.ping() is not None
    assert int(buddy.status_line()['fps']) >= 28


def test_list_the_animations(buddy):
    """`L`: every animation with its size, and the room left where they are kept."""
    folder = os.path.join(ROOT, 'sdcard', 'gifs')
    answer = json.loads(buddy.link(b'L')[9:])
    assert sorted(answer['gifs']) == sorted([name, os.path.getsize(os.path.join(folder, name))] for name in os.listdir(folder))
    assert answer['free_kb'] > 0 and 'more' not in answer


def test_own_picture_on_the_github_page(buddy):
    """`A` then `D` pieces: the app's picture on the GitHub page, cut to a circle; `A` of size 0 takes it away."""
    red = (0xF800).to_bytes(2, 'little') * (88 * 88)
    assert buddy.link(b'A' + (len(red) - 2).to_bytes(4, 'big')) is None, 'only a whole picture is taken'
    assert buddy.link(b'A' + len(red).to_bytes(4, 'big')) is not None
    for at in range(0, len(red), 1024):
        assert buddy.link(b'D' + at.to_bytes(4, 'big') + red[at:at + 1024], wait_ms=1500) is not None, 'piece at %d' % at
    buddy.run(20)
    assert buddy.show_page(6)
    buddy.run(1)
    shot = buddy.shot()
    assert shot.getpixel((60, 60)) == (255, 0, 0)
    assert shot.getpixel((20, 20)) == (0, 0, 0), 'the corners are outside the circle'
    assert buddy.link(b'A' + bytes(4)) is not None
    buddy.run(1)
    assert buddy.read_file('/avatar.bin') is None and buddy.shot().getpixel((60, 60)) != (255, 0, 0)


def test_card_test_passes(buddy):
    """`C`: the card test writes, reads back and deletes 256 KB, and the animations return to the card."""
    answer = buddy.link(b'C', wait_ms=20000)
    result = json.loads(answer[9:])
    assert result['test'] == 'passed' and result['card'] == 'SDHC' and result['signature'] is True
    assert result['partition'] == 'MSDOS5.0' and result['size_mb'] > 30000
    assert buddy.read_file('/buddy-test.bin', fs='sd') is None
    assert buddy.health()['gifs_from'] == 'card'
    usb = buddy.mb('SD', wait_s=20)
    assert usb['ok'] and usb['test'] == 'passed'


def test_unknown_messages_get_no_answer(buddy):
    """Unknown first bytes and broken JSON get no answer and do no harm."""
    for message in (b'Z', b'{"locked":', b'O{bad', b'G', b'B\x00\x00', b'D\x00\x00\x00\x00data'):
        assert buddy.link(message) is None, message
    assert buddy.ping() is not None
