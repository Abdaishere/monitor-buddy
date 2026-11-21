#!/usr/bin/env python3
"""Ask the Monitor Buddy over the Wi-Fi link how it is doing; --card also tests
its card (writes and reads back 256 KB).

    python tools/buddy_check.py [--card]
"""
import json
import sys

from buddy_link import ADDRESS_FILE, NO_LINK_KEY, link_cipher, send_wifi


def ask(cipher, address, question, timeout):
    for _ in range(3):
        answer = send_wifi(cipher, address, question, timeout=timeout)
        if answer is not None:
            return json.loads(answer[9:] or b'{}')
    sys.exit(f'The Buddy does not answer at {address}.')


def main():
    cipher = link_cipher() or sys.exit(NO_LINK_KEY)
    with open(ADDRESS_FILE) as file:
        address = file.read().strip()
    print(json.dumps(ask(cipher, address, b'H', 3), indent=1))
    if '--card' in sys.argv:
        print(json.dumps(ask(cipher, address, b'C', 30), indent=1))


if __name__ == '__main__':
    main()
