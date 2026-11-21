"""Firmware updates over the link, signed with the update key: docs/HANDOVER.md §4.4, §4.5."""
import hashlib
import os

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

from mbsim import KEYS, Restarted

PIECE = 1024


def image(size=180 * 1024, seed=b'new firmware'):
    """Something shaped like an app image: the magic byte, and an ELF hash at 0xB0 that names the build."""
    body = bytearray()
    counter = 0
    while len(body) < size:
        body += hashlib.sha256(seed + counter.to_bytes(4, 'big')).digest()
        counter += 1
    body = body[:size]
    body[0] = 0xE9
    body[0xB0:0xD0] = hashlib.sha256(seed).digest()
    return bytes(body)


def sign(data, key='update-key.pem'):
    with open(os.path.join(KEYS, key), 'rb') as file:
        return serialization.load_pem_private_key(file.read(), None).sign(data, ec.ECDSA(hashes.SHA256(), deterministic_signing=True))


def begin(b, data, signature):
    answer = b.link(b'B' + len(data).to_bytes(4, 'big') + signature, wait_ms=3000)
    return None if answer is None else int(answer[9:] or b'0')


def send(b, data, start=0, stop=None):
    """D pieces from `start`; the offset reached, and the last answer."""
    offset, answer = start, None
    stop = len(data) if stop is None else stop
    while offset < stop:
        last = offset + PIECE >= len(data)
        try:
            answer = b.link(b'D' + offset.to_bytes(4, 'big') + data[offset:offset + PIECE], wait_ms=3000, expect_restart=last)
        except Restarted:
            return offset + PIECE, b'restarted'
        if answer is None:
            break
        offset += PIECE
    return offset, answer


def test_update_happy_path(buddy, golden):
    """A signed image goes into the other slot; the Buddy restarts into it on trial, and the first packet keeps it."""
    data = image()
    old_build = buddy.ping()[:8]
    assert begin(buddy, data, sign(data)) == 0
    assert 'link: update of %d bytes' % len(data) in buddy.log
    offset, answer = send(buddy, data, stop=len(data) // 2 // PIECE * PIECE)
    golden('updating_50', buddy.shot())
    offset, answer = send(buddy, data, start=offset)
    assert answer == b'restarted' or buddy.run_until_restart(3), 'no restart after the last piece'
    assert 'link: update stored, restarting' in buddy.log
    buddy.run(12)
    assert 'link: listening on UDP 4747, new firmware on trial' in buddy.log
    assert buddy.board()['slot'] == 'app1' and buddy.board()['slot_state'] == 'pending'
    new_build = buddy.ping()[:8]
    assert new_build == data[0xB0:0xB8] and new_build != old_build, 'the build id is the new image\'s'
    assert 'link: new firmware kept' in buddy.log
    assert buddy.board()['slot_state'] == 'valid'
    assert buddy.health()['slot'] == 'app1' and buddy.health()['reset'] == 3
    buddy.power_cycle()
    buddy.run(12)
    assert buddy.board()['slot'] == 'app1', 'a kept firmware survives a power cut'


def test_update_rolls_back_without_a_packet(buddy):
    """A new firmware that restarts before any link packet reached it is undone: back to the slot before."""
    data = image(seed=b'never confirmed')
    assert begin(buddy, data, sign(data)) == 0
    offset, answer = send(buddy, data)
    assert answer == b'restarted' or buddy.run_until_restart(3)
    buddy.run(8)
    assert buddy.board()['slot'] == 'app1' and buddy.board()['slot_state'] == 'pending'
    buddy.power_cycle()                       # no packet came: the trial was never confirmed
    buddy.run(12)
    assert buddy.board()['slot'] == 'app0' and buddy.board()['slot_state'] == 'valid'
    assert 'new firmware on trial' not in buddy.log.split('ESP32-C6 Monitor-Buddy')[-1]
    assert buddy.ping() is not None


def test_update_with_the_wrong_key_is_refused(buddy):
    """An image signed with a key the Buddy does not hold: `refused`, no restart, the old firmware stays."""
    data = image(seed=b'forged')
    assert begin(buddy, data, sign(data, 'wrong-update-key.pem')) == 0
    offset, answer = send(buddy, data)
    assert answer is not None and answer[9:] == b'refused'
    assert 'link: update refused: the signature does not match' in buddy.log
    buddy.run(3)
    assert not buddy.restarted and buddy.board()['slot'] == 'app0'
    assert not os.path.exists(os.path.join(buddy.data, 'slot-app1.bin'))
    assert buddy.ping() is not None


def test_update_with_a_changed_image_is_refused(buddy):
    """One byte changed after signing: the signature no longer matches and the image is dropped."""
    data = image(seed=b'tampered')
    signature = sign(data)
    broken = data[:5000] + bytes([data[5000] ^ 0xFF]) + data[5001:]
    assert begin(buddy, broken, signature) == 0
    offset, answer = send(buddy, broken)
    assert answer[9:] == b'refused' and not buddy.restarted


def test_update_without_an_update_key_is_refused(make_buddy):
    """A Buddy that holds a link key but no update key takes no update at all."""
    import json
    from mbsim import LINK_KEY
    b = make_buddy(keys=False, files={'/keys.dat': json.dumps({'link_key': LINK_KEY, 'update_key': ''})})
    b.run(12)
    data = image()
    assert begin(b, data, sign(data)) is None
    assert 'link: update refused: no update key' in b.log


def test_update_too_big_is_refused(buddy):
    """An image larger than the 3 MB app slot is refused at the start."""
    signature = sign(b'x')
    assert buddy.link(b'B' + (0x300001).to_bytes(4, 'big') + signature, wait_ms=3000) is None
    assert 'link: update refused: Bad Size Given' in buddy.log
    assert buddy.ping() is not None


def test_update_resumes_after_a_break(buddy):
    """The link breaks mid-update: the Buddy pauses after 10 s, and the same `B` goes on where it stopped."""
    data = image(seed=b'resumed')
    signature = sign(data)
    assert begin(buddy, data, signature) == 0
    half = len(data) // 2 // PIECE * PIECE
    offset, _ = send(buddy, data, stop=half)
    buddy.run(12)
    assert 'link: update paused at %d of %d' % (half, len(data)) in buddy.log
    assert buddy.ping() is not None, 'the Buddy is back to normal while the update waits'
    buddy.run(120)
    assert begin(buddy, data, signature) == half, 'B again: the offset to go on from'
    assert 'link: update goes on at %d of %d' % (half, len(data)) in buddy.log
    offset, answer = send(buddy, data, start=half)
    assert answer == b'restarted' or buddy.run_until_restart(3)
    buddy.run(12)
    assert buddy.ping()[:8] == data[0xB0:0xB8]


def test_paused_update_is_given_up_after_10_minutes(buddy):
    """A paused update is dropped after 10 minutes; the same `B` then starts from 0."""
    data = image(seed=b'given up')
    signature = sign(data)
    begin(buddy, data, signature)
    send(buddy, data, stop=20 * PIECE)
    buddy.run(12)
    buddy.run(601)
    assert 'link: paused update given up' in buddy.log
    assert begin(buddy, data, signature) == 0


def test_repeated_piece_is_answered_not_written_twice(buddy):
    """A piece sent twice (its answer was lost) is answered again and written once."""
    data = image(seed=b'repeat')
    begin(buddy, data, sign(data))
    assert buddy.link(b'D' + (0).to_bytes(4, 'big') + data[:PIECE], wait_ms=3000) is not None
    assert buddy.link(b'D' + (0).to_bytes(4, 'big') + data[:PIECE], wait_ms=3000) is not None, 'the repeat is answered'
    assert buddy.link(b'D' + (5 * PIECE).to_bytes(4, 'big') + data[5 * PIECE:6 * PIECE], wait_ms=1500) is None, 'a piece from further on is not'
    offset, answer = send(buddy, data, start=PIECE)
    assert answer == b'restarted' or buddy.run_until_restart(3)
    buddy.run(12)
    assert open(os.path.join(buddy.data, 'slot-app1.bin'), 'rb').read() == data


def test_settings_change_during_trial_rolls_back(buddy):
    """`@MB SET` restarts the Buddy: on a firmware still on trial that undoes the update (HANDOVER §4.5)."""
    data = image(seed=b'set during trial')
    begin(buddy, data, sign(data))
    offset, answer = send(buddy, data)
    assert answer == b'restarted' or buddy.run_until_restart(3)
    buddy.run(8)
    assert buddy.board()['slot'] == 'app1'
    buddy.mb_restart('SET', {'config': {'theme': 1}})
    buddy.run(12)
    assert buddy.board()['slot'] == 'app0'
