#!/usr/bin/env python3
"""Monitor Buddy, the app: set this computer up, change anything on the Buddy
with a live picture of its screen, and show a line on it, without a terminal.

    python3 tools/buddy_app.py            open the app (Setup)
    python3 tools/buddy_app.py --say      open it at "Say something"
    python3 tools/buddy_app.py --demo     try it with a pretend Buddy; changes nothing on this computer
    python3 tools/buddy_app.py --yours    open it at "Make it yours" (the editor)
    python3 tools/buddy_app.py --no-open  start it without opening the browser (prints the address)
    python3 tools/buddy_app.py --board COM5   the Buddy's serial port, when it is not found by itself
    python3 tools/buddy_app.py --keep     keep running after the page is closed

It is a page in your browser, served by this program to this computer only.
Setup is done by the same code as ./setup.sh (tools/buddy_setup.py); a line
is sent by the same code as buddy-say. After the first setup it is in the app
menu as "Monitor Buddy".

"Make it yours" is the editor: the Buddy's settings are read over the USB
cable, every change shows at once on a preview of the 320x172 screen (the
firmware itself, built for the browser: tools/app/sim, see tools/sim), and
"Apply" writes them to the Buddy. What the Buddy held before each Apply is
kept on this computer, so "Undo" and "Earlier settings" bring it back.

Linux and Windows. On Windows the editor, the preview and Apply work (the
Buddy on a COM port); the background helper that Setup installs is for Linux.

How it stays private:
  - it listens on 127.0.0.1 only, on a port the system picks;
  - every request needs the random token of this run, which only the page it
    opened knows, and must name this address (so no other web page can use it);
  - it never sends a key to the page: like ./setup.sh it only says whether the
    keys are there.
It stops by itself a few minutes after the page was closed. Only Python's
standard library is used here.
"""
import argparse
import hmac
import json
import os
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import buddy_setup   # noqa: E402  (standard library only at import time)

PAGE = os.path.join(TOOLS, 'app', 'index.html')
ICON = os.path.join(TOOLS, 'app', 'icon.svg')
RUN_FILE = os.path.expanduser('~/.cache/monitor-buddy/app.json')   # where a running app is, for a second start
WINDOWS = os.name == 'nt'
LINUX = sys.platform == 'linux'
if WINDOWS:   # where a running app is, and the earlier settings
    DATA = os.path.join(os.environ.get('APPDATA') or os.path.expanduser('~'), 'monitor-buddy')
    RUN_FILE = os.path.join(DATA, 'app.json')
else:
    DATA = os.path.expanduser('~/.config/monitor-buddy')
SIM = os.path.join(TOOLS, 'app', 'sim')
# What the page may load besides itself: path -> (file, content type)
FILES = {'/icon.svg': (ICON, 'image/svg+xml'),
         '/sim/mbsim.js': (os.path.join(SIM, 'mbsim.js'), 'text/javascript'),
         '/sim/mbsim.wasm': (os.path.join(SIM, 'mbsim.wasm'), 'application/wasm'),
         '/sim/buddy-sim.js': (os.path.join(SIM, 'buddy-sim.js'), 'text/javascript')}
for _face in ('classic', 'cat', 'robot', 'minimal'):
    FILES['/img/face-%s.png' % _face] = (os.path.join(TOOLS, 'app', 'img', 'face-%s.png' % _face), 'image/png')
BACKUPS_KEPT = 30
ESPRESSIF = 0x303A   # the USB vendor of the Buddy's chip
IDLE_S = 180        # the page asks every 20 s while it is open
SAY_MAX = 100       # characters the Buddy has room for
OPTIONAL = ('odoo', 'claude', 'fish')
# The settings the app's "Make it yours" tab may change. Wi-Fi, the city and the
# stock and coin lists stay with the installer page, which has the searches for them.
EDITABLE = ('name', 'buddy_name', 'face', 'greeting', 'greeting_text', 'work', 'weekend', 'birthday', 'break_min', 'water',
            'message', 'theme', 'brightness', 'mount', 'clock_format', 'temp_unit', 'wind_unit', 'night', 'pages', 'github_user',
            'lat', 'lon', 'tz_name', 'tz_rule', 'stocks', 'coins', 'more_pages', 'page_order')
# Written but never read back: the page only learns whether there is one.
WRITE_ONLY = ('stock_key',)
ZONES = os.path.join(TOOLS, '..', 'web', 'vendor', 'posix_tz_db', 'zones.json')   # IANA zone -> POSIX rule, as the installer uses
CITY_SEARCH = 'https://geocoding-api.open-meteo.com/v1/search?count=8&language=en&format=json&name='
NO_BOARD = 'Plug the Buddy into this computer with its USB cable, then try again.'
PACKAGES = {'cryptography': 'python3-cryptography', 'serial': 'python3-serial', 'dateutil': 'python3-dateutil'}
if not LINUX:   # the editor needs the serial port only; the rest is the Linux helper's
    PACKAGES = {'serial': 'pyserial'}


def usb_ports():
    """The serial ports a Buddy may be on, the likeliest first (Windows and macOS; Linux has /dev/serial/by-id)."""
    from serial.tools import list_ports
    ports = sorted(list_ports.comports(), key=lambda port: port.device)
    return [port.device for port in ports if port.vid == ESPRESSIF]


class Recorder(buddy_setup.Setup):
    """A setup run that keeps what it says as a list the page can draw."""

    def __init__(self, *args, **options):
        self.events = []
        super().__init__(*args, **options)
        self.interactive = False   # nothing is asked: the page's tick boxes are the answers
        self.address_wait_s = 12   # the page is waiting

    def say(self, text=''):
        pass   # the lines of the terminal summary; the page draws its own from works and left

    def step(self, title):
        self.events.append({'kind': 'step', 'text': title.split('. ', 1)[-1]})

    def ok(self, text):
        self.events.append({'kind': 'ok', 'text': text})

    def info(self, text):
        text = text.replace(' (./setup.sh --ask-again to change)', ' (tick it above to change)')   # the terminal's wording
        self.events.append({'kind': 'info', 'text': text})

    def would(self, text):
        self.events.append({'kind': 'would', 'text': text})

    def todo(self, text, how):
        self.events.append({'kind': 'todo', 'text': text, 'how': how})
        self.left.append((text, how))


class App:
    """What the page can ask for. `make_setup(**options)` builds a Recorder;
    the tests and --demo pass one that touches nothing real."""

    def __init__(self, make_setup=Recorder, say=None, run=None, fetch=None, linux=LINUX, data=DATA, ports=usb_ports,
                 open_board=buddy_setup.Board, port=None):
        self._fetch = fetch or (lambda url: urllib.request.urlopen(url, timeout=10).read())
        self.make_setup = make_setup
        self.linux = linux           # False: no helper, no systemd; the Buddy is found among the serial ports
        self.data = data             # the folder of the earlier settings
        self._ports, self._open_board, self.port = ports, open_board, port
        self._say = say
        self._run = run or (lambda *cmd: subprocess.run(cmd, capture_output=True, text=True, timeout=600).returncode)
        self.lock = threading.Lock()
        self.job = None      # the setup that runs, or ran last: {'setup', 'done', 'failed'}

    # ── what is there ─────────────────────────────────────────────────────
    def missing_packages(self):
        missing = []
        for module, package in PACKAGES.items():
            try:
                __import__(module)
            except ImportError:
                missing.append(package)
        return missing

    def status(self):
        """A look that changes nothing: what works, what is left, the answers so far."""
        missing = self.missing_packages()
        if not self.linux:
            return {'missing': missing, 'can_install': False, 'command': 'python -m pip install ' + ' '.join(missing), 'helper': False,
                    'works': [], 'left': [], 'answers': {name: False for name in OPTIONAL}, 'done_before': False, 'events': []}
        if missing:
            return {'missing': missing, 'can_install': bool(shutil.which('pkexec') and shutil.which('apt-get')),
                    'command': 'sudo apt install ' + ' '.join(missing)}
        setup = self.make_setup(dry=True)
        setup.main(status_only=True)
        return {'missing': [], 'works': setup.works, 'left': [{'text': t, 'how': h} for t, h in setup.left],
                'answers': {name: bool(setup.state['answers'].get(name, False)) for name in OPTIONAL},
                'done_before': bool(setup.state['done']), 'events': setup.events}

    def install_packages(self):
        """Ask the system (its own password window) to install what Python lacks."""
        missing = self.missing_packages()
        if not missing:
            return {'ok': True}
        if not (shutil.which('pkexec') and shutil.which('apt-get')):
            return {'ok': False, 'error': 'This system has no graphical installer I can ask. Install these with your package manager: ' + ', '.join(missing)}
        code = self._run('pkexec', 'apt-get', 'install', '-y', *missing)
        if code in (126, 127):
            return {'ok': False, 'error': 'Nothing was installed: the password window was closed.'}
        if code != 0 or self.missing_packages():
            return {'ok': False, 'error': 'The install did not finish. Check your internet connection and try again.'}
        return {'ok': True}

    # ── setup ─────────────────────────────────────────────────────────────
    def start_setup(self, choices):
        """Runs the setup in the background with the page's tick boxes as the answers."""
        if not self.linux:
            return {'ok': False, 'error': 'The background helper is for Linux. On this computer the app edits and previews the Buddy.'}
        with self.lock:
            if self.job and not self.job['done']:
                return {'ok': False, 'error': 'Setup is already running.'}
            setup = self.make_setup(dry=False)
            for name in OPTIONAL:
                if isinstance(choices.get(name), bool):
                    setup.state['answers'][name] = choices[name]
            setup.repair_ok = choices.get('repair') is True
            setup._save_state()
            job = self.job = {'setup': setup, 'done': False, 'failed': None}

        def work():
            try:
                if setup.main() != 0:
                    job['failed'] = 'Setup stopped early. The steps above say why; nothing is broken, and you can run it again.'
            except Exception as error:   # show it instead of a page that waits for ever
                job['failed'] = f'Something unexpected went wrong ({type(error).__name__}: {error}). Nothing is broken; try again.'
            job['done'] = True

        threading.Thread(target=work, daemon=True).start()
        return {'ok': True}

    def progress(self):
        job = self.job
        if not job:
            return {'running': False, 'events': []}
        setup = job['setup']
        answer = {'running': not job['done'], 'events': list(setup.events), 'failed': job['failed']}
        if job['done']:
            answer.update(works=setup.works, left=[{'text': t, 'how': h} for t, h in setup.left])
        return answer

    # ── the Buddy's settings, over the cable ──────────────────────────────
    def _with_board(self, work):
        """Runs work(board) with the Buddy's port open; the helper lets go of it meanwhile."""
        if self.missing_packages():
            return {'ok': False, 'error': 'Finish the setup first.'}
        if not self.linux:
            return self._with_port(work)
        with self.lock:
            if self.job and not self.job['done']:
                return {'ok': False, 'error': 'Setup is running. Try again when it is done.'}
            setup = self.make_setup(dry=False)
            port = setup.find_port()
            if not port:
                return {'ok': False, 'error': NO_BOARD}
            helper = setup.run('systemctl', '--user', 'is-active', buddy_setup.UNIT)[1] == 'active'
            if helper:
                setup.run('systemctl', '--user', 'stop', buddy_setup.UNIT)
            board = None
            try:
                board = setup.open_board(port)
                return work(board)
            except (OSError, TimeoutError):
                return {'ok': False, 'error': 'The Buddy did not answer on the cable. Unplug it, plug it in again and try once more. '
                                              'If the installer page is open in a browser tab, close that tab first.'}
            finally:
                if board is not None:
                    board.close()
                if helper:
                    setup.run('systemctl', '--user', 'start', buddy_setup.UNIT)

    def _with_port(self, work):
        """The same without the Linux helper: the Buddy is the first Espressif serial port (or the one --port names)."""
        with self.lock:
            try:
                ports = [self.port] if self.port else self._ports()
            except Exception:   # no pyserial port list on this system
                ports = []
            if not ports:
                return {'ok': False, 'error': NO_BOARD}
            for port in ports:
                board = None
                try:
                    board = self._open_board(port)
                    return work(board)
                except (OSError, TimeoutError):
                    continue    # busy, or not a Buddy: the next one
                finally:
                    if board is not None:
                        board.close()
            return {'ok': False, 'error': 'The Buddy did not answer on the cable (%s). Unplug it, plug it in again and try once more. '
                                          'If the installer page or a serial monitor is open, close it first.' % ', '.join(ports)}

    def settings(self):
        def read(board):
            config = board.get_config()
            if config is None:
                return {'ok': False, 'error': 'The Buddy did not show its settings. Try again in a moment.'}
            answer = {'ok': True, 'config': {key: config[key] for key in EDITABLE if key in config},
                      'has': {key: bool(config.get(key)) for key in WRITE_ONLY if key in config}}
            try:
                info = board.info()
                wifi = info.get('wifi') or {}
                answer['wifi'] = {'ssid': wifi.get('ssid', ''), 'connected': bool(wifi.get('connected'))}
                answer['fw'] = str(info.get('fw', ''))
            except (OSError, TimeoutError):
                pass
            answer['backups'] = self.backups()['backups'][:1]
            return answer
        return self._with_board(read)

    # ── the settings before each Apply, kept on this computer ─────────────
    def _backup_dir(self):
        return os.path.join(self.data, 'backups')

    def _keep(self, config):
        """Writes what the Buddy holds now to the backups (never a key). The file's name, or None."""
        kept = {key: config[key] for key in EDITABLE if key in config}
        folder = self._backup_dir()
        try:
            os.makedirs(folder, mode=0o700, exist_ok=True)
            names = sorted(name for name in os.listdir(folder) if name.endswith('.json'))
            if names:   # the same as the newest: one copy is enough
                with open(os.path.join(folder, names[-1])) as file:
                    if json.load(file).get('config') == kept:
                        return names[-1][:-5]
            stamp = time.strftime('%Y%m%d-%H%M%S')
            name = stamp
            count = 1
            while os.path.exists(os.path.join(folder, name + '.json')):
                count += 1
                name = '%s-%d' % (stamp, count)
            with open(os.path.join(folder, name + '.json'), 'w') as file:
                json.dump({'when': time.strftime('%Y-%m-%d %H:%M:%S'), 'config': kept}, file)
            for old in names[:max(0, len(names) + 1 - BACKUPS_KEPT)]:
                os.remove(os.path.join(folder, old))
            return name
        except (OSError, ValueError):
            return None

    def backups(self):
        """The earlier settings, the newest first: [{id, when}]."""
        found = []
        try:
            for name in sorted(os.listdir(self._backup_dir()), reverse=True):
                if not name.endswith('.json'):
                    continue
                try:
                    with open(os.path.join(self._backup_dir(), name)) as file:
                        found.append({'id': name[:-5], 'when': str(json.load(file).get('when', ''))})
                except (OSError, ValueError):
                    continue
        except OSError:
            pass
        return {'ok': True, 'backups': found}

    def restore(self, ident):
        """Puts an earlier set of settings back on the Buddy (what it holds now is kept first, so this too can be undone)."""
        ident = str(ident)
        if not ident or any(c not in '0123456789-' for c in ident):
            return {'ok': False, 'error': 'No such earlier settings.'}
        try:
            with open(os.path.join(self._backup_dir(), ident + '.json')) as file:
                wanted = json.load(file)['config']
        except (OSError, ValueError, KeyError):
            return {'ok': False, 'error': 'Those earlier settings are no longer on this computer.'}

        def write(board):
            now = board.get_config()
            if now is None:
                return {'ok': False, 'error': 'The Buddy did not show its settings. Try again in a moment.'}
            patch = {key: value for key, value in wanted.items() if key in EDITABLE and key in now and now[key] != value}
            if not patch:
                return {'ok': True, 'same': True, 'config': {key: now[key] for key in EDITABLE if key in now}}
            self._keep(now)
            ok, errors = board.set_config(patch, None)
            if not ok:
                return {'ok': False, 'errors': errors, 'error': 'The Buddy did not take the earlier settings; nothing was changed.'}
            now.update(patch)
            return {'ok': True, 'config': {key: now[key] for key in EDITABLE if key in now}, 'backups': self.backups()['backups'][:1]}
        return self._with_board(write)

    def city(self, name):
        """Places called `name`, each with the coordinates, zone and clock rule the Buddy needs (Open-Meteo's search)."""
        name = ' '.join(str(name).split())[:60]
        if not name:
            return {'ok': True, 'places': []}
        try:
            found = json.loads(self._fetch(CITY_SEARCH + urllib.parse.quote(name))).get('results') or []
            with open(ZONES) as file:
                zones = json.load(file)
        except (OSError, ValueError):
            return {'ok': False, 'error': 'The city search did not answer. Check the internet connection and try again.'}
        places = []
        for place in found:
            zone = place.get('timezone')
            if zone in zones and isinstance(place.get('latitude'), (int, float)) and isinstance(place.get('longitude'), (int, float)):
                label = ', '.join(part for part in (place.get('name'), place.get('admin1'), place.get('country')) if part)
                places.append({'label': label, 'lat': '%.4f' % place['latitude'], 'lon': '%.4f' % place['longitude'],
                               'tz_name': zone, 'tz_rule': zones[zone]})
        return {'ok': True, 'places': places}

    def save_settings(self, patch, wifi=None):
        patch = {key: value for key, value in patch.items() if key in EDITABLE + WRITE_ONLY} if isinstance(patch, dict) else {}
        if not (isinstance(wifi, dict) and isinstance(wifi.get('ssid'), str) and wifi['ssid'] and isinstance(wifi.get('password'), str)):
            wifi = None
        else:
            wifi = {'ssid': wifi['ssid'], 'password': wifi['password']}
        if not patch and not wifi:
            return {'ok': False, 'error': 'Nothing to save.'}

        def write(board):
            now = board.get_config()
            if now is not None:
                unknown = sorted(key for key in patch if key not in now and key not in WRITE_ONLY)
                if unknown:   # the firmware would skip them without a word
                    return {'ok': False, 'needs_update': unknown,
                            'error': "This Buddy's firmware is older and has no setting for: %s. Update the Buddy from the installer page, "
                                     'then apply again. Nothing was changed.' % ', '.join(unknown)}
                self._keep(now)
            ok, errors = board.set_config(patch, wifi)
            if ok:
                return {'ok': True, 'backups': self.backups()['backups'][:1]}
            if errors:
                return {'ok': False, 'errors': errors, 'error': 'The Buddy did not take that. The marked fields say why; nothing was changed.'}
            return {'ok': False, 'error': 'The Buddy did not confirm the change. Open this tab again to see what it holds.'}
        return self._with_board(write)

    # ── say ───────────────────────────────────────────────────────────────
    def say(self, text):
        text = ' '.join(str(text).split())[:SAY_MAX]
        if not text:
            return {'ok': False, 'error': 'Type something first.'}
        if self._say is None:
            if not self.linux:
                return {'ok': False, 'error': 'A line goes to the Buddy over Wi-Fi with the key the Linux setup makes. '
                                              'This computer has not been paired that way.'}
            if self.missing_packages():
                return {'ok': False, 'error': 'Finish the setup first.'}
            import buddy_say
            self._say = buddy_say.say
        problem = self._say(text)
        return {'ok': problem is None, 'error': problem, 'text': text}


def make_handler(app, token, state):
    class Handler(BaseHTTPRequestHandler):
        server_version = 'MonitorBuddy'

        def log_message(self, *args):
            pass

        def _send(self, code, body, kind='application/json'):
            data = body if isinstance(body, bytes) else json.dumps(body).encode()
            self.send_response(code)
            self.send_header('Content-Type', kind)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            # 'self' and 'wasm-unsafe-eval': the preview is the firmware as WebAssembly, served from here
            self.send_header('Content-Security-Policy', "default-src 'none'; style-src 'unsafe-inline'; "
                             "script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval'; img-src 'self' data: blob:; connect-src 'self'")
            self.end_headers()
            self.wfile.write(data)

        def _allowed(self):
            """This address, and the token of this run."""
            port = self.server.server_address[1]
            if self.headers.get('Host') not in (f'127.0.0.1:{port}', f'localhost:{port}'):
                return False
            return hmac.compare_digest(self.headers.get('X-Buddy-Token', ''), token)

        def do_GET(self):
            state['seen'] = time.monotonic()
            path = self.path.split('?', 1)[0]
            port = self.server.server_address[1]
            if self.headers.get('Host') not in (f'127.0.0.1:{port}', f'localhost:{port}'):
                return self._send(403, {'error': 'not for you'})
            if path == '/':   # the page holds nothing private; it reads the token from its own address
                with open(PAGE, 'rb') as file:
                    return self._send(200, file.read(), 'text/html; charset=utf-8')
            if path in FILES:   # nothing private: pictures, and the preview (the firmware built for the browser)
                try:
                    with open(FILES[path][0], 'rb') as file:
                        return self._send(200, file.read(), FILES[path][1])
                except OSError:
                    return self._send(404, {'error': 'not found'})
            if not self._allowed():
                return self._send(403, {'error': 'not for you'})
            if path == '/api/status':
                return self._send(200, app.status())
            if path == '/api/progress':
                return self._send(200, app.progress())
            if path == '/api/settings':
                return self._send(200, app.settings())
            if path == '/api/backups':
                return self._send(200, app.backups())
            if path == '/api/city':
                query = urllib.parse.parse_qs(self.path.partition('?')[2]).get('q', [''])[0]
                return self._send(200, app.city(query))
            if path == '/api/ping':
                return self._send(200, {'ok': True})
            self._send(404, {'error': 'not found'})

        def do_POST(self):
            state['seen'] = time.monotonic()
            if not self._allowed():
                return self._send(403, {'error': 'not for you'})
            try:
                size = int(self.headers.get('Content-Length', '0'))
                body = json.loads(self.rfile.read(size) or b'{}') if 0 <= size <= 16384 else None
            except ValueError:
                body = None
            if not isinstance(body, dict):
                return self._send(400, {'error': 'bad request'})
            path = self.path.split('?', 1)[0]
            if path == '/api/setup':
                return self._send(200, app.start_setup(body))
            if path == '/api/settings':
                return self._send(200, app.save_settings(body.get('config'), body.get('wifi')))
            if path == '/api/restore':
                return self._send(200, app.restore(body.get('id', '')))
            if path == '/api/say':
                return self._send(200, app.say(body.get('text', '')))
            if path == '/api/install':
                return self._send(200, app.install_packages())
            self._send(404, {'error': 'not found'})

    return Handler


def serve(app, port=0):
    """(server, token, state): the app on 127.0.0.1, not yet serving."""
    token = secrets.token_urlsafe(24)
    state = {'seen': time.monotonic()}
    server = ThreadingHTTPServer(('127.0.0.1', port), make_handler(app, token, state))
    server.daemon_threads = True
    return server, token, state


def demo_app():
    """The app against a pretend Buddy in a temporary folder: for a look, and for screenshots."""
    import buddy_flash
    import buddy_link
    import mb_serial
    home = tempfile.mkdtemp(prefix='monitor-buddy-demo-')
    config = os.path.join(home, '.config', 'monitor-buddy')
    buddy_link.LINK_KEY_FILE = os.path.join(config, 'link-key')
    buddy_flash.SIGNING_KEY = os.path.join(config, 'update-key.pem')
    buddy_link.CONFIG_H = os.path.join(home, 'no-config.h')
    buddy_link.ADDRESS_FILE = os.path.join(home, '.cache', 'monitor-buddy', 'address')
    os.makedirs(os.path.join(home, '.config', 'google-chrome'))
    os.makedirs(os.path.join(home, '.claude'))

    class Board:
        link = update = None

        def info(self):
            return {'ok': True, 'fw': '1.0.0', 'wifi': {'saved': True, 'ssid': 'HomeNet', 'connected': True},
                    'keys': {'link': mb_serial.fingerprint('link', self.link) if self.link else False,
                             'update': mb_serial.fingerprint('update', self.update) if self.update else False}}

        def write_keys(self, body):
            time.sleep(1.5)
            Board.link, Board.update = body['link_key'], body['update_key']
            return True

        config = {'name': 'Sam', 'buddy_name': 'Pixel', 'face': 'cat', 'greeting': 'salam', 'greeting_text': '',
                  'work': {'start': 9, 'end': 17, 'lunch': 12}, 'weekend': 'sat-sun', 'birthday': '03-14', 'break_min': 50,
                  'water': False, 'message': "You've got this", 'theme': 2, 'mount': 'left', 'clock_format': '24h',
                  'temp_unit': 'celsius', 'wind_unit': 'kmh', 'night': {'enabled': True, 'start': 23, 'end': 7, 'brightness': 26},
                  'pages': {'face': True, 'clock': True, 'date': True, 'weather': True, 'moon': False, 'stock': False,
                            'github': True, 'coin': False}, 'github_user': 'octocat', 'brightness': 180,
                  'lat': '38.7167', 'lon': '-9.1333', 'tz_name': 'Europe/Lisbon', 'tz_rule': 'WET0WEST,M3.5.0/1,M10.5.0',
                  'stocks': [{'symbol': 'AAPL', 'on': True}], 'stock_key': '',
                  'coins': [{'pair': 'BTC-USD', 'source': 'coinbase', 'on': True}],
                  'more_pages': {'abda': True, 'odoo': True, 'prs': True, 'gifs': True, 'today': True, 'game': True, 'claude': True},
                  'page_order': []}

        def get_config(self):
            return json.loads(json.dumps(Board.config))

        def set_config(self, patch, wifi=None):
            time.sleep(1.5)
            for key, value in patch.items():
                if isinstance(value, dict):
                    Board.config[key].update(value)
                else:
                    Board.config[key] = value
            return True, {}

        def close(self):
            pass

    started = []

    def make(**options):
        setup = Recorder(home=home, **options)
        setup.address_wait_s = 0
        setup.find_port = lambda: '/dev/ttyDEMO'
        setup.open_board = lambda port: Board()
        setup.ping = lambda: bool(Board.link)
        setup.groups = lambda: ({'dialout'}, True)
        setup.which = lambda name: None if name == 'pw-dump' else '/usr/bin/' + name

        def run(*cmd, timeout=30):
            time.sleep(0.15)
            if cmd[:2] == ('systemctl', '--user') and cmd[2] in ('start', 'restart'):
                started.append(1)
            if cmd[:3] == ('systemctl', '--user', 'is-active'):
                return 0, 'active' if started else 'inactive'
            return 0, ''

        setup.run = run
        return setup

    return App(make_setup=make, say=lambda text: None if Board.link else 'Finish the setup first.', linux=True,
               data=os.path.join(home, '.config', 'monitor-buddy'))


def running():
    """The address of an app that already runs, or None."""
    try:
        with open(RUN_FILE) as file:
            there = json.load(file)
        # it answers on its port (os.kill(pid, 0) only asks on Linux; on Windows it ends the process)
        port = urllib.parse.urlsplit(there['url']).port
        with socket.create_connection(('127.0.0.1', port), timeout=1):
            return there['url']
    except (OSError, ValueError, KeyError, TypeError):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--say', action='store_true', help='open at "Say something"')
    parser.add_argument('--demo', action='store_true', help='a pretend Buddy; changes nothing')
    parser.add_argument('--no-open', action='store_true', help='do not open the browser')
    parser.add_argument('--yours', action='store_true', help='open at "Make it yours"')
    parser.add_argument('--board', metavar='PORT', help='the serial port of the Buddy (Windows: COM5), when it is not found by itself')
    parser.add_argument('--keep', action='store_true', help='keep running after the page is closed (open the app again to get the page back)')
    parser.add_argument('--port', type=int, default=0, help=argparse.SUPPRESS)
    args = parser.parse_args()
    tab = '&tab=say' if args.say else '&tab=yours' if args.yours or not LINUX else ''
    url = None if args.demo else running()
    if url:   # one app is enough: show the one that runs
        if not args.no_open:
            webbrowser.open(url + tab)
        print(url + tab)
        return
    server, token, state = serve(demo_app() if args.demo else App(port=args.board), args.port)
    url = f'http://127.0.0.1:{server.server_address[1]}/#t={token}' + ('&demo=1' if args.demo else '')
    if not args.demo:
        os.makedirs(os.path.dirname(RUN_FILE), mode=0o700, exist_ok=True)
        with os.fdopen(os.open(RUN_FILE, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), 'w') as file:
            json.dump({'pid': os.getpid(), 'url': url}, file)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print(url + tab, flush=True)
    if not args.no_open:
        webbrowser.open(url + tab)
    try:
        while args.keep or time.monotonic() - state['seen'] < IDLE_S:
            time.sleep(2)
    except KeyboardInterrupt:
        pass
    finally:
        server.shutdown()
        if not args.demo and running() == url:
            os.remove(RUN_FILE)


if __name__ == '__main__':
    main()
