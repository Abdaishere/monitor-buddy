#!/usr/bin/env python3
"""Send animations to a Monitor Buddy over the Wi-Fi link, or remove them.

    python tools/buddy_put.py FILE.gif [FILE.gif ...]
    python tools/buddy_put.py --remove NAME.gif [NAME.gif ...]

The Buddy keeps them in /gifs, on its card or else in its internal file system,
and plays one on each visit of its GIF page. Make a GIF fit the Buddy first with
tools/buddy_gif.py.
"""
import os
import sys

from buddy_flash import PIECE, send
from buddy_link import ADDRESS_FILE, NO_LINK_KEY, link_cipher


def main():
    remove = sys.argv[1:2] == ['--remove']
    names = sys.argv[2:] if remove else sys.argv[1:]
    if not names:
        sys.exit(__doc__)
    cipher = link_cipher() or sys.exit(NO_LINK_KEY)
    with open(ADDRESS_FILE) as file:
        address = file.read().strip()
    send(cipher, address, b'P', f'The Buddy does not answer at {address}.')
    for path in names:
        name = os.path.basename(path).encode()
        if remove:
            send(cipher, address, b'X' + name, f'The Buddy did not remove {path}.')
            print('removed', path)
            continue
        with open(path, 'rb') as file:
            data = file.read()
        send(cipher, address, b'F' + len(data).to_bytes(4, 'big') + name, f'The Buddy refused {path}: its name, or no room left.')
        for offset in range(0, len(data), PIECE):
            send(cipher, address, b'D' + offset.to_bytes(4, 'big') + data[offset:offset + PIECE],
                 f'The Buddy stopped answering at byte {offset} of {path}.')
        print('sent', path, len(data), 'bytes', flush=True)


if __name__ == '__main__':
    main()
