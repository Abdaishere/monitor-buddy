#!/usr/bin/env python3
"""Screenshot the Monitor Buddy over the Wi-Fi link.

    python tools/buddy_shot.py OUT.png [PAGE]

PAGE is a PAGE_* number from src/main.cpp (0 face, 9 Odoo, 10 pull requests,
12 Today, 14 Claude, 15 Yesterday); the Buddy turns to it first.
"""
import sys
import time

from buddy_flash import deliver
from buddy_link import ADDRESS_FILE, NO_LINK_KEY, link_cipher
from PIL import Image

W, H = 320, 172


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cipher = link_cipher() or sys.exit(NO_LINK_KEY)
    with open(ADDRESS_FILE) as file:
        address = file.read().strip()
    if len(sys.argv) > 2:
        deliver(cipher, address, b'G' + bytes([int(sys.argv[2])])) or sys.exit(f'The Buddy does not answer at {address}.')
        time.sleep(1.5)
    pixels = bytearray()
    for row in range(0, H, 2):
        answer = deliver(cipher, address, b'R' + bytes([row])) or sys.exit(f'The Buddy stopped answering at row {row}.')
        pixels += answer[9:]
    values = (pixels[i] | pixels[i + 1] << 8 for i in range(0, W * H * 2, 2))
    image = Image.new('RGB', (W, H))
    image.putdata([((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31) for v in values])
    image.save(sys.argv[1])


if __name__ == '__main__':
    main()
