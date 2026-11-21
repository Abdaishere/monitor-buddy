#!/usr/bin/env python3
"""Chrome's native messaging host for tools/chrome-odoo. Chrome starts it for
that extension only (allowed_origins in its host manifest), once per message:

  {"say": "text"}   from the extension's popup: show that line on the Buddy
                    (the same as buddy-say); the answer says whether it got there
  anything else     the Odoo summary: saved to ODOO_FILE, where buddy_link.py
                    picks it up
"""
import json
import os
import struct
import sys

ODOO_FILE = os.path.expanduser('~/.cache/monitor-buddy/odoo.json')
MESSAGE_MAX = 64 * 1024


def main():
    size = struct.unpack('<I', sys.stdin.buffer.read(4))[0]
    if size > MESSAGE_MAX:
        sys.exit('message too big')
    data = json.loads(sys.stdin.buffer.read(size))
    if not isinstance(data, dict):
        sys.exit('not an object')
    if set(data) == {'say'}:
        return reply(say(data['say']))
    os.makedirs(os.path.dirname(ODOO_FILE), mode=0o700, exist_ok=True)
    part = ODOO_FILE + '.part'
    with os.fdopen(os.open(part, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), 'w') as file:
        json.dump(data, file)
    os.replace(part, ODOO_FILE)
    reply({'ok': True})


def say(text):
    """Show a line on the Buddy; {'ok': True} or {'ok': False, 'error': why}."""
    text = ' '.join(str(text).split())[:100]
    if not text:
        return {'ok': False, 'error': 'Type something first.'}
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import buddy_say
        problem = buddy_say.say(text)
    except ImportError:
        problem = 'This computer is not set up for the Buddy yet. Open Monitor Buddy and click Set up first.'
    return {'ok': problem is None, 'error': problem}


def reply(answer):
    data = json.dumps(answer).encode()
    sys.stdout.buffer.write(struct.pack('<I', len(data)) + data)
    sys.stdout.buffer.flush()


if __name__ == '__main__':
    main()
