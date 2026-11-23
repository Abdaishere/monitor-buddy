#!/usr/bin/env python3
"""Show a line on the Monitor Buddy for 20 seconds.

    buddy-say TEXT...

Room for about 100 characters. A tap on the Buddy puts the line away.
"""
import sys

from buddy_link import ADDRESS_FILE, NO_ADDRESS, NO_LINK_KEY, link_cipher, send_wifi


def say(text):
    """Show text on the Buddy. Returns None when it took it, else a sentence
    that says why not (the app shows it; the command exits with it)."""
    cipher = link_cipher()
    if cipher is None:
        return NO_LINK_KEY
    try:
        with open(ADDRESS_FILE) as file:
            address = file.read().strip()
    except OSError:
        return NO_ADDRESS
    for _ in range(3):
        if send_wifi(cipher, address, b'S' + text.encode()[:110]) is not None:
            return None
    return f'The Buddy does not answer at {address}.'


def main():
    text = ' '.join(sys.argv[1:]).strip()
    if not text:
        sys.exit(__doc__)
    sys.exit(say(text))


if __name__ == '__main__':
    main()
