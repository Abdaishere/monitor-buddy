#!/usr/bin/env python3
"""The two keys this computer shares with a Monitor Buddy.

  link key     64 hex digits (AES-256). Seals every packet of the Wi-Fi link.
               Kept in buddy_link.LINK_KEY_FILE.
  update key   An ECDSA P-256 key. Its private half signs firmware updates and
               stays in buddy_flash.SIGNING_KEY; the Buddy gets the public half.

Rules, all in this file:
  - the keys are made on this computer, in files only their owner can read
    (mode 600 in a folder of mode 700);
  - a key that is there is never replaced, except by rotate(), which
    tools/buddy_setup.py runs only for --rotate-keys with the Buddy on USB;
  - no function here prints a key, and nothing but for_board() hands one out.

Run it to see what is there (names and fingerprints only):

    python tools/buddy_keys.py
"""
import os
import re
import secrets
import stat
import time

import buddy_flash
import buddy_link


def _private(path):
    """Make an existing key file and its folder readable by the owner only."""
    if stat.S_IMODE(os.stat(path).st_mode) != 0o600:
        os.chmod(path, 0o600)
    folder = os.path.dirname(path)
    if stat.S_IMODE(os.stat(folder).st_mode) & 0o077:
        os.chmod(folder, 0o700)


def _read_link_file():
    try:
        with open(buddy_link.LINK_KEY_FILE) as file:
            key = file.read().strip()
    except OSError:
        return None
    return key if re.fullmatch(r'[0-9a-fA-F]{64}', key) else None


def _write_new(path, text):
    """Write a key file that must not exist yet (O_EXCL), mode 600."""
    os.makedirs(os.path.dirname(path), mode=0o700, exist_ok=True)
    with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as file:
        file.write(text)


def have():
    """Which keys this computer holds: {'link': bool, 'update': bool}."""
    return {'link': _read_link_file() is not None, 'update': os.path.exists(buddy_flash.SIGNING_KEY)}


def ensure():
    """Make whichever key is missing; leave the ones that are there untouched.
    Returns what happened per key: 'made', 'kept', or for the link key 'adopted'
    when a build of your own already had one as LINK_KEY in config/config.h."""
    done = {}
    if _read_link_file() is not None:
        done['link'] = 'kept'
    else:
        if os.path.exists(buddy_link.LINK_KEY_FILE):
            raise ValueError(f'{buddy_link.LINK_KEY_FILE} does not hold 64 hex digits. Look at it, or move it away, and run setup again.')
        own = buddy_link.link_key()   # no file, so this can only come from config.h
        _write_new(buddy_link.LINK_KEY_FILE, (own or secrets.token_hex(32)) + '\n')
        done['link'] = 'adopted' if own else 'made'
    _private(buddy_link.LINK_KEY_FILE)
    if os.path.exists(buddy_flash.SIGNING_KEY):
        buddy_flash.public_key()   # raises when the file is not a usable key
        done['update'] = 'kept'
    else:
        buddy_flash.new_key()
        done['update'] = 'made'
    _private(buddy_flash.SIGNING_KEY)
    return done


def for_board():
    """The body of "@MB KEYS": what the Buddy needs. The link key, and only the
    PUBLIC half of the update key. Goes over USB and nowhere else."""
    return {'link_key': _read_link_file(), 'update_key': buddy_flash.public_key()}


def fingerprints():
    """What the Buddy's INFO shows when it holds this computer's keys."""
    from mb_serial import fingerprint   # needs pyserial, which only the USB steps need
    body = for_board()
    return {'link': fingerprint('link', body['link_key']), 'update': fingerprint('update', body['update_key'])}


def rotate():
    """Move both keys aside and make new ones. Returns (undo, forget): undo()
    puts the old keys back (when the Buddy did not take the new ones), forget()
    deletes the old ones for good (when it did)."""
    stamp = time.strftime('%Y%m%d-%H%M%S')
    moved = []
    for path in (buddy_link.LINK_KEY_FILE, buddy_flash.SIGNING_KEY):
        if os.path.exists(path):
            os.rename(path, f'{path}.old-{stamp}')
            moved.append(path)
    _write_new(buddy_link.LINK_KEY_FILE, secrets.token_hex(32) + '\n')
    buddy_flash.new_key()

    def undo():
        for path in (buddy_link.LINK_KEY_FILE, buddy_flash.SIGNING_KEY):
            if os.path.exists(path):
                os.remove(path)
        for path in moved:
            os.rename(f'{path}.old-{stamp}', path)

    def forget():
        for path in moved:
            old = f'{path}.old-{stamp}'
            with open(old, 'r+b') as file:   # overwrite before the unlink
                file.write(secrets.token_bytes(os.path.getsize(old)))
            os.remove(old)

    return undo, forget


def main():
    there = have()
    if not all(there.values()):
        print('Keys on this computer: ' + ', '.join(f'{name}: {"yes" if ok else "none yet"}' for name, ok in there.items()))
        print('Run ./setup.sh to make them.')
        return
    prints = fingerprints()
    print(f'Keys on this computer: link (fingerprint {prints["link"]}), update (fingerprint {prints["update"]}).')
    print('A Buddy that holds them shows the same fingerprints: python tools/mb_serial.py PORT info')


if __name__ == '__main__':
    main()
