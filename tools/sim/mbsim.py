"""Drive a simulated Monitor Buddy from Python.

    from mbsim import Buddy
    with Buddy() as buddy:                    # boots the firmware on the fake board
        buddy.run(10)                         # ten simulated seconds
        buddy.state(locked=False, prs=3)      # what buddy_link.py would send
        buddy.double_knock()
        buddy.shot().save('hello.png')        # the screen, 320x172

The simulator (`.build/mbsim`, built by `make`) runs the real `src/main.cpp`.
Time is simulated: `run()` returns as fast as the PC computes it, and the
same script gives the same pixels every time.
"""
import json
import os
import re
import select
import shutil
import subprocess
import tempfile
from datetime import datetime, timedelta, timezone

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
# MBSIM_SCREEN=WxH drives the build `make SCREEN=WxH` made; without it, the real board's 320x172.
SCREEN = os.environ.get('MBSIM_SCREEN', '')
NATIVE = SCREEN in ('', '320x172')
BINARY = os.environ.get('MBSIM_BINARY') or os.path.join(HERE, '.build' if not SCREEN else '.build-' + SCREEN, 'mbsim')
KEYS = os.path.join(HERE, 'keys')
LINK_KEY = open(os.path.join(KEYS, 'link-key.txt')).read().strip()


def _make_test_keys():
    """The update keys of the tests: made here, not stored (*.pem stays out of git). Fixed numbers, so the same everywhere."""
    import hashlib
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    order = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551      # of P-256
    for name in ('update-key', 'wrong-update-key'):
        path = os.path.join(KEYS, name + '.pem')
        if os.path.exists(path) and (name != 'update-key' or os.path.exists(os.path.join(KEYS, name + '.pub.pem'))):
            continue
        number = int.from_bytes(hashlib.sha256(b'monitor-buddy simulator ' + name.encode()).digest(), 'big') % (order - 1) + 1
        key = ec.derive_private_key(number, ec.SECP256R1())
        with open(path, 'wb') as file:
            file.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        if name == 'update-key':
            with open(os.path.join(KEYS, name + '.pub.pem'), 'wb') as file:
                file.write(key.public_key().public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo))


_make_test_keys()
UPDATE_KEY_PEM = open(os.path.join(KEYS, 'update-key.pub.pem')).read()
DUBAI = timezone(timedelta(hours=4))
MONDAY_8AM = datetime(2026, 1, 5, 8, 0, 0, tzinfo=DUBAI)   # the default boot time: a Monday morning
WIDTH, HEIGHT = (int(n) for n in SCREEN.split('x')) if SCREEN else (320, 172)
CX, CY = WIDTH // 2, HEIGHT // 2
SWIPE_X = (70, WIDTH - 70) if WIDTH >= 240 else (WIDTH * 22 // 100, WIDTH * 78 // 100)   # a narrow panel has no room for 180 px
WIFI = ('HomeWiFi', 'password123')
REST = (0.0, -1.0, 0.0)    # the pull on a Buddy standing in its holder, in g
FLAT = (0.0, 0.0, 1.0)     # lying on its back


class Restarted(Exception):
    """The firmware restarted where the script did not expect it."""


class Buddy:
    """One simulated board. Its flash and card live in `data` and survive restarts."""

    def __init__(self, data=None, when=MONDAY_8AM, wifi=WIFI, aps=None, internet=None, card='ok', seed=1,
                 binary=BINARY, config=None, files=None, sd_files=None, imu=True, touch=True, accel=REST,
                 gifs=True, uptime_ms=0, extra_args=(), boot=True, keys=True):
        self.binary = binary
        self.own_data = data is None
        self.data = data or tempfile.mkdtemp(prefix='mbsim-')
        self.true_ms = int(when.timestamp() * 1000)
        self.rtc_ms = 0                    # the chip's clock: unset until SNTP
        self.reset_reason = 1
        self.aps = list(aps) if aps is not None else ([wifi + (-55,)] if wifi else [])
        self.internet = internet           # a MockInternet, or None for a router without internet
        self.card = card
        self.seed = seed
        self.imu, self.touch_chip, self.accel = imu, touch, accel
        self.uptime_ms = uptime_ms
        self.extra_args = list(extra_args)
        self.proc = None
        self.log = ''                      # everything the Buddy printed on its serial port
        self.boots = 0
        self.t_ms = 0
        self.restarted = False             # set by any command during which the firmware restarted
        self.timeout_s = 300               # real seconds one command may take
        self.cipher = AESGCM(bytes.fromhex(LINK_KEY))
        self._nonce_ms = 0
        os.makedirs(os.path.join(self.data, 'littlefs'), exist_ok=True)
        if wifi:
            self.write_file('/wifi.json', json.dumps({'ssid': wifi[0], 'password': wifi[1]}))
        if keys:   # a Buddy that went through setup: both keys, as "@MB KEYS" stores them
            self.write_file('/keys.dat', json.dumps({'link_key': LINK_KEY, 'update_key': UPDATE_KEY_PEM}))
        if config is not None:
            self.write_file('/config.json', config if isinstance(config, str) else json.dumps(config))
        for path, content in (files or {}).items():
            self.write_file(path, content)
        if card == 'ok':
            os.makedirs(os.path.join(self.data, 'sd', 'gifs'), exist_ok=True)
            if gifs:
                for name in sorted(os.listdir(os.path.join(ROOT, 'sdcard', 'gifs'))):
                    shutil.copy(os.path.join(ROOT, 'sdcard', 'gifs', name), os.path.join(self.data, 'sd', 'gifs', name))
        for path, content in (sd_files or {}).items():
            self.write_file(path, content, fs='sd')
        if boot:
            self.boot()

    # ── life cycle ────────────────────────────────────────────
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        self._stop()
        if self.own_data:
            shutil.rmtree(self.data, ignore_errors=True)

    def _stop(self):
        if self.proc:
            try:
                self.proc.stdin.close()
            except OSError:
                pass
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            self.proc = None

    def boot(self, reason=None):
        """Power on (or restart with `reason`, see esp_reset_reason())."""
        self._stop()
        if reason is not None:
            self.reset_reason = reason
        args = [self.binary, '--data', self.data, '--epoch', str(self.true_ms // 1000), '--rtc', str(self.rtc_ms // 1000),
                '--reset-reason', str(self.reset_reason), '--seed', str(self.seed + self.boots), '--card', self.card,
                '--accel', '%g,%g,%g' % tuple(self.accel)]
        for ssid, password, *rssi in self.aps:
            args += ['--ap', '%s:%s:%d' % (ssid, password, rssi[0] if rssi else -55)]
        if self.internet is None:
            args.append('--no-internet')
        else:
            args += ['--http-mock', self.internet.address]
        if not self.imu:
            args.append('--no-imu')
        if not self.touch_chip:
            args.append('--no-touch')
        if self.uptime_ms:
            args += ['--uptime-ms', str(self.uptime_ms)]
        self.proc = subprocess.Popen(args + self.extra_args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.boots += 1
        self.t_ms = self.uptime_ms
        self._boot_true_ms = self.true_ms - self.uptime_ms
        self.restarted = False

    def power_cycle(self):
        """Pull the plug: the chip's clock is lost, the flash stays."""
        self.rtc_ms = 0
        self.boot(reason=1)

    def crash(self):
        """A panic: the board restarts with reset reason 4 and a crash dump."""
        self.extra_args = [a for a in self.extra_args if a != '--crash-dump'] + ['--crash-dump']
        self.boot(reason=4)

    def cmd(self, line, expect_restart=False):
        self.proc.stdin.write(line + '\n')
        self.proc.stdin.flush()
        if not select.select([self.proc.stdout], [], [], self.timeout_s)[0]:
            self.proc.kill()
            raise RuntimeError('the simulator hung (no answer in %d s) at: %s' % (self.timeout_s, line))
        text = self.proc.stdout.readline()
        if not text:
            raise RuntimeError('the simulator died (exit code %s) at: %s' % (self.proc.wait(), line))
        answer = json.loads(text)
        if answer.get('event') == 'crash':
            raise RuntimeError('the firmware crashed with signal %s at: %s' % (answer['signal'], line))
        self.t_ms, self.true_ms, self.rtc_ms = answer['t_ms'], answer['true_ms'], answer['rtc_ms']
        if answer['restart']:
            self._drain_after_exit()
            self.boot(reason=answer['reason'])
            self.restarted = True
            if not expect_restart:
                raise Restarted(line)
        return answer

    def _drain_after_exit(self):
        self.proc.wait(timeout=5)
        self.proc = None

    # ── time ──────────────────────────────────────────────────
    def run(self, seconds, **stop):
        """Let `seconds` of simulated time pass."""
        return self.run_ms(int(round(seconds * 1000)), **stop)

    def run_ms(self, ms, udp=False, serial=None, expect_restart=False):
        line = 'run %d' % ms
        if udp:
            line += ' udp'
        if serial:
            line += ' serial ' + serial.encode().hex()
        answer = self.cmd(line, expect_restart=expect_restart)
        self._pull_log()
        return answer

    @property
    def now(self):
        """The time out in the simulated world."""
        return datetime.fromtimestamp(self.true_ms / 1000, DUBAI)

    def run_until(self, when, step_s=600, each=None):
        """Run to a wall-clock time (a datetime, or 'HH:MM' today or tomorrow). `each()` runs every step."""
        if isinstance(when, str):
            hour, minute = map(int, when.split(':'))
            target = self.now.replace(hour=hour, minute=minute, second=0, microsecond=0)
            if target <= self.now:
                target += timedelta(days=1)
            when = target
        while self.now < when:
            self.run(min(step_s, (when - self.now).total_seconds()))
            if each:
                each()

    def run_until_restart(self, seconds=5):
        """Run until the firmware restarts; True when it did."""
        self.restarted = False
        try:
            self.run(seconds, expect_restart=True)
        except Restarted:
            pass
        return self.restarted

    # ── serial ────────────────────────────────────────────────
    def _pull_log(self):
        if not self.proc:
            return ''
        text = bytes.fromhex(self.cmd('serial_out')['hex']).decode('utf-8', 'replace')
        self.log += text
        return text

    def lines(self, since=0):
        return self.log[since:].replace('\r', '').split('\n')

    def mark(self):
        """A place in the log, for `log_since()`."""
        self._pull_log()
        return len(self.log)

    def log_since(self, mark):
        self._pull_log()
        return self.log[mark:].replace('\r', '')

    def status_line(self):
        """The last `app=.. mood=..` line as a dict."""
        self._pull_log()
        found = re.findall(r'^app=.*$', self.log.replace('\r', ''), re.M)
        return dict(pair.split('=', 1) for pair in found[-1].split()) if found else {}

    def page(self):
        self.run(1.3)
        return int(self.status_line().get('app', -1))

    def mood(self):
        self.run(1.3)
        return int(self.status_line().get('mood', -1))

    def serial_write(self, text):
        self.cmd('serial_in ' + text.encode().hex())

    def mb(self, command, body=None, wait_s=3, expect_restart=False):
        """One `@MB` command over USB serial; its JSON answer."""
        mark = self.mark()
        self.serial_write('@MB %s%s\n' % (command, ' ' + json.dumps(body) if body is not None else ''))
        try:
            self.run(wait_s, serial='"cmd":"%s"' % command[:16], expect_restart=expect_restart)
            self.run_ms(40, expect_restart=expect_restart)
        except Restarted:
            raise
        for line in self.log_since(mark).split('\n'):
            if line.startswith('@MB {'):
                try:
                    answer = json.loads(line[4:])
                except ValueError:
                    continue
                if answer.get('cmd') == command[:16]:
                    return answer
        return None

    def mb_restart(self, command, body=None):
        """An `@MB` command that restarts the Buddy (SET, KEYS, DEFAULTS, REBOOT): its answer, after the restart."""
        self.restarted = False
        answer = None
        try:
            answer = self.mb(command, body, expect_restart=True)
            self.run(2, expect_restart=True)
        except Restarted:
            pass
        if not self.restarted:
            raise AssertionError('@MB %s did not restart the Buddy: %r' % (command, answer))
        return answer

    def usb_shot(self):
        """The screen as `@MB SHOT` sends it over USB (what tools/buddy_shot.py reads)."""
        import base64
        mark = self.mark()
        self.serial_write('@MB SHOT\n')
        self.run(4, serial='"cmd":"SHOT"')
        self.run_ms(40)
        rows = {}
        for line in self.log_since(mark).split('\n'):
            if line.startswith('@SHOT '):
                _, y, data = line.split(' ', 2)
                rows[int(y)] = base64.b64decode(data)
        if len(rows) != HEIGHT:
            return None
        return _rgb565(b''.join(rows[y] for y in range(HEIGHT)), WIDTH, HEIGHT)

    # ── the Wi-Fi link (what tools/buddy_link.py speaks) ───────
    def seal(self, message, stamp_ms=None, key=None):
        """A link packet: nonce (the sender's clock in ms, 4 more bytes), ciphertext, tag."""
        if stamp_ms is None:
            stamp_ms = max(self.true_ms, self._nonce_ms + 1)
            self._nonce_ms = stamp_ms
        nonce = int(stamp_ms).to_bytes(8, 'big') + (self.boots * 65536 + (stamp_ms % 65536)).to_bytes(4, 'big')
        cipher = AESGCM(key) if key else self.cipher
        return nonce, nonce + cipher.encrypt(nonce, message, None)

    def link_raw(self, packet, wait_ms=400, expect_restart=False):
        """Send bytes to UDP 4747; the raw answers."""
        self.cmd('udp_out')
        self.cmd('udp ' + packet.hex())
        try:
            self.run_ms(wait_ms, udp=True, expect_restart=expect_restart)
        except Restarted:
            raise
        return [bytes.fromhex(p) for p in self.cmd('udp_out')['packets']]

    def link(self, message, wait_ms=400, stamp_ms=None, key=None, expect_restart=False):
        """Send one link message. The answer after 'ok' (8 bytes build id, a status byte, extra data), or None."""
        if isinstance(message, str):
            message = message.encode()
        nonce, packet = self.seal(message, stamp_ms, key)
        for answer in self.link_raw(packet, wait_ms, expect_restart=expect_restart):
            try:
                plain = self.cipher.decrypt(bytes([nonce[0] | 0x80]) + nonce[1:], answer, None)
            except InvalidTag:
                continue
            if plain.startswith(b'ok'):
                return plain[2:]
        return None

    def ping(self):
        return self.link(b'P')

    def state(self, **fields):
        """A STATE as buddy_link.py sends it (docs/HANDOVER.md §6.3). True when the Buddy answered."""
        return self.link(json.dumps(fields, separators=(',', ':'))) is not None

    def odoo(self, **fields):
        return self.link(b'O' + json.dumps(fields, separators=(',', ':')).encode()) is not None

    def say(self, text):
        return self.link(b'S' + text.encode()) is not None

    def health(self):
        answer = self.link(b'H')
        return json.loads(answer[9:]) if answer else None

    def status_bits(self):
        """The status byte of a ping: bit 0 timer, 1 animations on the card, 2 join, 3 lock."""
        answer = self.ping()
        return answer[8] if answer else None

    def show_page(self, page):
        return self.link(b'G' + bytes([page])) is not None

    def link_shot(self):
        """The screen read over the link, two rows at a time (what tools/buddy_shot.py does over Wi-Fi)."""
        rows = b''
        for y in range(0, HEIGHT, 2):
            answer = self.link(b'R' + bytes([y]))
            if answer is None:
                return None
            rows += answer[9:]
        return _rgb565(rows, WIDTH, HEIGHT)

    # ── the screen ────────────────────────────────────────────
    def shot(self):
        """What the LCD shows now, as the user sees it."""
        path = os.path.join(self.data, 'shot.raw')
        answer = self.cmd('shot ' + path)
        with open(path, 'rb') as file:
            return _rgb565(file.read(), answer['w'], answer['h'])

    def board(self):
        """The board's state: backlight, wifi, portal, slot ..."""
        return self.cmd('state')

    # ── fingers ───────────────────────────────────────────────
    def touch(self, x, y):
        self.cmd('touch %d %d' % (x, y))

    def release(self):
        self.cmd('release')

    def tap(self, x=CX, y=CY, ms=90, settle_ms=450):
        self.touch(x, y)
        self.run_ms(ms)
        self.release()
        self.run_ms(settle_ms)

    def double_tap(self, x=CX, y=CY):
        self.tap(x, y, settle_ms=180)
        self.tap(x, y)

    def swipe(self, x0, y0, x1, y1, ms=240, steps=8, settle_ms=450):
        for i in range(steps + 1):
            self.touch(x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps)
            self.run_ms(max(ms // steps, 1))
        self.release()
        self.run_ms(settle_ms)

    def swipe_left(self):
        self.swipe(SWIPE_X[1], CY, SWIPE_X[0], CY)

    def swipe_right(self):
        self.swipe(SWIPE_X[0], CY, SWIPE_X[1], CY)

    def swipe_up(self, distance=70):
        self.swipe(CX, HEIGHT - 42, CX, HEIGHT - 42 - distance)

    def swipe_down(self, distance=70):
        self.swipe(CX, 40, CX, 40 + distance)

    def hold(self, seconds, x=CX, y=CY, expect_restart=False):
        self.touch(x, y)
        self.run(seconds, expect_restart=expect_restart)
        self.release()
        self.run_ms(300, expect_restart=expect_restart)

    def press_boot(self, ms=150):
        self.cmd('boot 1')
        self.run_ms(ms)
        self.cmd('boot 0')
        self.run_ms(100)

    # ── motion ────────────────────────────────────────────────
    def pull(self, x, y, z):
        """The acceleration the sensor feels, in g."""
        self.cmd('accel %g %g %g' % (x, y, z))

    def turn(self, x=0.0, y=0.0, z=0.0):
        """The rotation the sensor feels, in degrees a second."""
        self.cmd('gyro %g %g %g' % (x, y, z))

    def knock(self, g=0.9, ms=6, rest=None):
        """One knock on its head: a short jolt of `g`."""
        rest = rest or self.accel
        self.pull(rest[0] + g, rest[1], rest[2])
        self.run_ms(ms)
        self.pull(*rest)

    def double_knock(self, g=0.9, gap_ms=260, lead_ms=400):
        """Knock, knock. Then the quiet the detector waits for."""
        self.run_ms(lead_ms)
        self.knock(g)
        self.run_ms(gap_ms)
        self.knock(g)
        self.run_ms(500)

    def shake(self, seconds=1.2, g=2.6):
        """Shake it: the pull jumps between readings."""
        rest = self.accel
        for i in range(int(seconds * 1000 / 80)):
            self.pull(rest[0], rest[1] - (g if i % 2 else -g * 0.6), rest[2])
            self.run_ms(80)
        self.pull(*rest)

    def lay_flat(self):
        self.accel = FLAT
        self.pull(*FLAT)

    def stand_up(self):
        self.accel = REST
        self.pull(*REST)

    def spin(self, seconds=0.8, dps=300):
        self.turn(z=dps)
        self.run(seconds)
        self.turn()

    # ── surroundings ──────────────────────────────────────────
    def wifi_down(self):
        self.cmd('ap_clear')

    def wifi_up(self, ssid=WIFI[0], password=WIFI[1], rssi=-55):
        self.cmd('ap_add %s %s %d' % (ssid.encode().hex(), password.encode().hex() or '-', rssi))

    def set(self, key, value):
        return self.cmd('set %s %s' % (key, value))

    def internet_down(self):
        self.set('internet', 0)

    def internet_up(self):
        self.set('internet', 1)

    def portal_save(self, ssid, password):
        """What a phone does on the setup hotspot's page: save a network. The Buddy restarts."""
        try:
            self.cmd('portal_save %s %s' % (ssid.encode().hex(), password.encode().hex()), expect_restart=True)
        except Restarted:
            pass

    # ── the board's files ─────────────────────────────────────
    def _host(self, path, fs='littlefs'):
        return os.path.join(self.data, fs, path.lstrip('/'))

    def write_file(self, path, content, fs='littlefs'):
        host = self._host(path, fs)
        os.makedirs(os.path.dirname(host), exist_ok=True)
        with open(host, 'wb') as file:
            file.write(content if isinstance(content, bytes) else content.encode())

    def read_file(self, path, fs='littlefs'):
        try:
            with open(self._host(path, fs), 'rb') as file:
                return file.read()
        except OSError:
            return None

    def read_json(self, path, fs='littlefs'):
        content = self.read_file(path, fs)
        return json.loads(content) if content else None

    def files(self, fs='littlefs'):
        root = os.path.join(self.data, fs)
        return sorted(os.path.relpath(os.path.join(folder, name), root)
                      for folder, _, names in os.walk(root) for name in names)


def _rgb565(data, width, height):
    """RGB565 little-endian bytes to an image."""
    return Image.frombytes('RGB', (width, height), data, 'raw', 'BGR;16')


def diff(a, b):
    """How many pixels differ between two images, and a picture of where."""
    from PIL import ImageChops
    if a.size != b.size:
        return a.size[0] * a.size[1], None
    delta = ImageChops.difference(a.convert('RGB'), b.convert('RGB')).convert('L').point(lambda v: 255 if v else 0)
    count = delta.histogram()[255]
    return count, delta
