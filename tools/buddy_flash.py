#!/usr/bin/env python3
"""Update a Monitor Buddy's firmware over the Wi-Fi link.

    python tools/buddy_flash.py [FIRMWARE.bin]
    python tools/buddy_flash.py --new-key    # make SIGNING_KEY, print its UPDATE_KEY line

FIRMWARE is the "app" file of a release (monitor-buddy-vX.Y.Z-esp32c6-app.bin);
without it, the build in .pio/build/esp32-c6/firmware.bin. It is signed with
SIGNING_KEY: the Buddy takes no image whose signature does not match the update
key it was given (./setup.sh does that over USB; a build of your own may carry
it as UPDATE_KEY in config/config.h). It keeps a new firmware once it answered
on the link again; one that does not is undone at the Buddy's next restart.
"""
import json
import os
import sys
import time

from buddy_link import ADDRESS_FILE, NO_LINK_KEY, link_cipher, send_wifi
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

FIRMWARE = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '.pio', 'build', 'esp32-c6', 'firmware.bin')
BUILD_AT = 0xB0   # the ELF's SHA-256 in the image, app_elf_sha256 of esp_app_desc_t
PIECE = 1024
SIGNING_KEY = os.path.expanduser('~/.config/monitor-buddy/update-key.pem')
TRIES = 8
TIMEOUT_S = 1
RESTART_S = 90
RESUME_S = 600   # the Buddy keeps a broken-off update that long
REFUSED = 'The Buddy refused the firmware: its signature does not match UPDATE_KEY, or the image is broken.'


def deliver(cipher, address, message):
    """The Buddy's answer to message, or None after TRIES tries."""
    for _ in range(TRIES):
        if (answer := send_wifi(cipher, address, message, timeout=TIMEOUT_S)) is not None:
            return answer
    return None


def send(cipher, address, message, failure):
    return deliver(cipher, address, message) or sys.exit(failure)


def _public_pem(key):
    return key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode()


def new_key(path=None):
    """Writes a new signing key to SIGNING_KEY (never over an old one: that
    raises FileExistsError) and returns its public half in PEM, which the Buddy
    needs."""
    path = path or SIGNING_KEY
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    key = ec.generate_private_key(ec.SECP256R1())
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'wb') as file:
        file.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    return _public_pem(key)


def public_key(path=None):
    """The public half, in PEM, of the signing key that is there."""
    with open(path or SIGNING_KEY, 'rb') as file:
        return _public_pem(serialization.load_pem_private_key(file.read(), None))


def is_app_image(image):
    """True for an application image (what goes over Wi-Fi); False for a factory
    image, which starts with the bootloader and holds the partition table at 0x8000."""
    return len(image) > BUILD_AT + 32 and image[0] == 0xE9 and image[0x8000:0x8002] != b'\xaa\x50'


def main():
    if sys.argv[1:] == ['--new-key']:
        try:
            public = new_key()
        except FileExistsError:
            sys.exit(f'{SIGNING_KEY} is already there. It is not replaced: see "Rotate the update key" in docs/HANDOVER.md.')
        print('#define UPDATE_KEY "' + public.replace('\n', '\\n') + '"')
        return
    with open(sys.argv[1] if len(sys.argv) > 1 else FIRMWARE, 'rb') as file:
        image = file.read()
    if not is_app_image(image):
        sys.exit('This is not an "app" firmware file. Over Wi-Fi the Buddy takes the file whose name ends in -app.bin; '
                 'the -factory.bin file is for the web installer and USB.')
    build = image[BUILD_AT:BUILD_AT + 8]
    cipher = link_cipher() or sys.exit(NO_LINK_KEY)
    with open(ADDRESS_FILE) as file:
        address = file.read().strip()
    if send(cipher, address, b'P', f'The Buddy does not answer at {address}.')[:8] == build:
        print('The Buddy already runs this firmware.')
        return
    with open(SIGNING_KEY, 'rb') as file:
        # RFC 6979: the same image always gets the same signature, so a second run resumes the first
        signature = serialization.load_pem_private_key(file.read(), None).sign(image, ec.ECDSA(hashes.SHA256(), deterministic_signing=True))
    begin = b'B' + len(image).to_bytes(4, 'big') + signature
    give_up, sent_all = time.monotonic() + RESUME_S, False
    while time.monotonic() < give_up:
        answer = deliver(cipher, address, begin)   # says where to go on from
        if answer is None:
            time.sleep(2)
            continue
        offset = int(answer[9:] or b'0')
        if sent_all and offset == 0:   # it had them all and threw them away
            sys.exit(REFUSED)
        print(f'{offset * 100 // len(image)}%', flush=True)
        while offset < len(image):
            answer = deliver(cipher, address, b'D' + offset.to_bytes(4, 'big') + image[offset:offset + PIECE])
            if answer is None:
                break
            if answer[9:] == b'refused':
                sys.exit(REFUSED)
            offset += PIECE
            if offset % (PIECE * 200) == 0:
                print(f'{offset * 100 // len(image)}%', flush=True)
        if offset + PIECE < len(image):   # the link broke off: the Buddy keeps what it has
            continue
        sent_all = True
        sent_at = time.monotonic()   # the last piece went: the Buddy restarts into the new firmware
        while time.monotonic() - sent_at < RESTART_S:
            answer = deliver(cipher, address, b'H')
            if answer is not None:
                if answer[:8] == build:
                    print('The Buddy runs the new firmware.')
                    return
                if json.loads(answer[9:])['up_s'] < time.monotonic() - sent_at:
                    sys.exit('The Buddy does not run the new firmware: it is back on the one before.')
                break   # no restart: the last piece did not arrive
            time.sleep(2)
        else:
            sys.exit('The Buddy does not answer since the last piece.')
    sys.exit('The Buddy stopped answering for 10 minutes. It keeps its firmware.')


if __name__ == '__main__':
    main()
