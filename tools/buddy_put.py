#!/usr/bin/env python3
"""Send animations to a Monitor Buddy over the Wi-Fi link, or remove them.

    python tools/buddy_put.py FILE.gif [FILE.gif ...]
    python tools/buddy_put.py --remove NAME.gif [NAME.gif ...]

The Buddy keeps them in /gifs, on its card or else in its internal file system,
and plays one on each visit of its GIF page. Make a GIF fit the Buddy first with
tools/buddy_gif.py. The app (tools/buddy_app.py) sends them, and the GitHub
page's own picture, with the functions here.
"""
import json
import os
import sys

from buddy_flash import PIECE, deliver
from buddy_link import ADDRESS_FILE, NO_ADDRESS, NO_LINK_KEY, link_cipher

PICTURE_BYTES = 88 * 88 * 2   # the GitHub page's picture: 88x88 RGB565, little-endian, row by row


class Refused(Exception):
    """Why the Buddy did not do it, as a sentence for the user."""


def buddy():
    """(cipher, address) of the Buddy this computer is paired with."""
    cipher = link_cipher()
    if cipher is None:
        raise Refused(NO_LINK_KEY)
    try:
        with open(ADDRESS_FILE) as file:
            return cipher, file.read().strip()
    except OSError:
        raise Refused(NO_ADDRESS) from None


def ask(cipher, address, message, failure):
    """The Buddy's answer to message (the build id, the status byte, then any text)."""
    answer = deliver(cipher, address, message)
    if answer is None:
        raise Refused(failure)
    return answer


def upload(cipher, address, head, data, what, refused):
    """head (F or A with the size), then data in D pieces."""
    ask(cipher, address, head, refused)
    for offset in range(0, len(data), PIECE):
        ask(cipher, address, b'D' + offset.to_bytes(4, 'big') + data[offset:offset + PIECE],
            f'The Buddy stopped answering at byte {offset} of {what}.')


def put(cipher, address, name, data):
    """One animation, under name: letters, digits, dot, dash and underscore, ending in .gif."""
    upload(cipher, address, b'F' + len(data).to_bytes(4, 'big') + name.encode(), data, name,
           f'The Buddy refused {name}: its name, or no room left.')


def remove(cipher, address, name):
    ask(cipher, address, b'X' + name.encode(), f'The Buddy did not remove {name}.')


def animations(cipher, address):
    """{"gifs": [[name, bytes], ...], "free_kb": room left}, and "more": true when not all fitted the answer."""
    return json.loads(ask(cipher, address, b'L', f'The Buddy does not answer at {address}.')[9:])


def picture(cipher, address, pixels):
    """The GitHub page's own picture (PICTURE_BYTES), or b'' to show the GitHub one again."""
    upload(cipher, address, b'A' + len(pixels).to_bytes(4, 'big'), pixels, 'the picture', 'The Buddy refused the picture.')


def main():
    removing = sys.argv[1:2] == ['--remove']
    paths = sys.argv[2:] if removing else sys.argv[1:]
    if not paths:
        sys.exit(__doc__)
    try:
        cipher, address = buddy()
        ask(cipher, address, b'P', f'The Buddy does not answer at {address}.')
        for path in paths:
            name = os.path.basename(path)
            if removing:
                remove(cipher, address, name)
                print('removed', name)
                continue
            with open(path, 'rb') as file:
                data = file.read()
            put(cipher, address, name, data)
            print('sent', name, len(data), 'bytes', flush=True)
    except Refused as error:
        sys.exit(str(error))


if __name__ == '__main__':
    main()
