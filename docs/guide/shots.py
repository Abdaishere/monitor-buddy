"""Drive the Buddy through steps over USB and Wi-Fi and lay its screenshots on one sheet.

    shots.py STEPS.json OUT.png [SHOT_DIR]

A step: {"name": ..., "state": {...} or null, "say": text or null, "wait": seconds}.
"""
import base64
import glob
import json
import os
import sys
import termios
import time
import tty

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.expanduser('~/Monitor-Buddy/tools'))
steps = json.load(open(sys.argv[1]))
sys.argv = sys.argv[:1] + sys.argv[2:]
out, shot_dir = sys.argv[1], (sys.argv[2] if len(sys.argv) > 2 else None)
sys.argv = ['x']
import buddy_link

W, H = 320, 172
BASE = {'locked': False, 'mic': False, 'prs': 3, 'prs_failing': 0, 'reviews': 2, 'changes': 1, 'claude': 0, 'at': '',
        'use': [62, 31], 'pr_list': [{'t': '#123 Add feature', 's': 2, 'r': 1},
                                     {'t': '#124 Fix sync queue', 's': 1, 'r': 2},
                                     {'t': '#125 Update docs', 's': 3, 'r': 0}]}
fd = os.open(glob.glob('/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_B0*')[0], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
tty.setraw(fd)
attrs = termios.tcgetattr(fd)
attrs[2] &= ~termios.HUPCL
termios.tcsetattr(fd, termios.TCSANOW, attrs)
log = b''

def pump(seconds, until=None):
    global log
    end, got = time.monotonic() + seconds, b''
    while time.monotonic() < end:
        try:
            got += os.read(fd, 65536)
        except BlockingIOError:
            time.sleep(0.005)
        if until and until in got:
            break
    log += got
    return got

def shot():
    for _ in range(4):
        os.write(fd, b'@MB SHOT\n')
        rows = {}
        for line in pump(8, b'"cmd":"SHOT"').split(b'\n'):
            if line.startswith(b'@SHOT '):
                try:
                    _, y, data = line.strip().split(b' ', 2)
                    raw = base64.b64decode(data, validate=True)
                    if len(raw) == W * 2:
                        rows[int(y)] = raw
                except ValueError:
                    pass
        if len(rows) == H:
            image = Image.new('RGB', (W, H))
            image.putdata([((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31)
                           for y in range(H) for v in (rows[y][2 * x] | rows[y][2 * x + 1] << 8 for x in range(W))])
            return image
    return Image.new('RGB', (W, H), (255, 0, 255))

cipher, address = buddy_link.link_cipher(), open(buddy_link.ADDRESS_FILE).read().strip()
pump(1)
sheet = Image.new('RGB', (3 * (W + 8) + 8, ((len(steps) + 2) // 3) * (H + 26) + 8), (40, 40, 48))
draw = ImageDraw.Draw(sheet)
for index, step in enumerate(steps):
    if step.get('state') is not None:
        os.write(fd, f'@MB STATE {json.dumps({**BASE, **step["state"]})}\n'.encode())
        pump(2, b'"cmd":"STATE"')
    if step.get('say'):
        print('say answered:', buddy_link.send_wifi(cipher, address, b'S' + step['say'].encode()) is not None)
    pump(step.get('wait', 1.2))
    image = shot()
    if shot_dir:
        image.save(os.path.join(shot_dir, step['name'] + '.png'))
    x, y = 8 + index % 3 * (W + 8), 8 + index // 3 * (H + 26)
    sheet.paste(image, (x, y + 16))
    draw.text((x, y), step['name'], fill=(255, 255, 255))
sheet.save(out)
for line in log.decode(errors='replace').splitlines():
    if line.startswith(('mood:', 'link:')) or 'Guru' in line or 'rst:' in line or 'app=13' in line:
        print(line[:110])
