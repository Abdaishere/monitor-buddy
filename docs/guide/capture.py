"""Screenshot every page and face of the Monitor Buddy over serial."""
import base64, json, os, sys, termios, time, tty
sys.path.insert(0, os.path.expanduser('~/Monitor-Buddy/tools'))
from PIL import Image

W, H = 320, 172
sys.argv = ['x']
import buddy_link
PRS = buddy_link.pull_requests() or (0, 0, [])
fd = os.open('/dev/ttyACM0', os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
tty.setraw(fd)
a = termios.tcgetattr(fd); a[2] &= ~termios.HUPCL; a[4] = a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
buf = b''

def pump(seconds, until=None):
    global buf
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = os.read(fd, 65536)
        except BlockingIOError:
            chunk = b''
        if chunk:
            buf += chunk
            if until and until in buf:
                return True
        else:
            time.sleep(0.005)
    return False

def state(**extra):
    global buf
    body = {'locked': False, 'prs': PRS[0], 'prs_failing': 0, 'claude': 0, 'pr_list': PRS[2], **extra}
    buf = b''
    os.write(fd, f'@MB STATE {json.dumps(body)}\n'.encode())
    pump(2, b'"cmd":"STATE"')

def shot(name):
    global buf
    for attempt in range(4):
        buf = b''
        os.write(fd, b'@MB SHOT\n')
        pump(8, b'"cmd":"SHOT"')
        rows = {}
        for line in buf.split(b'\n'):
            if line.startswith(b'@SHOT '):
                try:
                    _, y, data = line.strip().split(b' ', 2)
                    raw = base64.b64decode(data, validate=True)
                    if len(raw) == W * 2: rows[int(y)] = raw
                except Exception:
                    pass
        if len(rows) == H:
            img = Image.new('RGB', (W, H)); px = img.load()
            for y in range(H):
                r = rows[y]
                for x in range(W):
                    v = r[2 * x] | r[2 * x + 1] << 8
                    px[x, y] = ((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31)
            img.save(f'shots/{name}.png'); print('ok', name, flush=True); return True
        print('retry', name, len(rows), flush=True)
    return False

print('waiting for odoo data', pump(150, b'[net] odoo ok'), flush=True)
pump(3)
MOODS = ['normal', 'happy', 'surprised', 'sleepy', 'angry', 'sad', 'excited', 'love', 'suspicious']
for i, name in enumerate(MOODS):
    state(demo=100, page=0); state(demo=100, mood=i, page=0); pump(1.6); shot(f'mood_{i}_{name}')
STATUS = {2: ('lunch', 4), 3: ('away', 3), 4: ('nudge', 3), 5: ('ci', 3), 6: ('lockedin', 3),
          7: ('focus', 1), 8: ('breaktime', 1), 9: ('claude_needs', 1), 10: ('claude_done', 1), 1: ('night', 1)}
for demo, (name, frames) in STATUS.items():
    state(demo=100, page=0); pump(0.3); state(demo=demo, page=0); pump(1.8)
    for f in range(frames):
        shot(f'status_{name}_{f}'); pump(0.12 if name != 'lunch' else 0.07)
for page, name in ((1, 'clock'), (2, 'date'), (3, 'weather'), (4, 'moon'), (6, 'github'), (9, 'odoo'), (10, 'prs')):
    state(demo=100, page=page); pump(1.0); shot(f'page_{name}')
state(demo=100, page=6, tap=True); pump(1.0); shot('page_github_heatmap')
state(demo=100, page=9, tap=True); pump(1.0); shot('page_odoo_task2')
for i in range(10):
    state(demo=100, page=8); pump(2.3); shot(f'page_abda_{i}')
state(page=0)
