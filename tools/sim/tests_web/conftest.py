"""The browser installer (web/) against the simulated firmware.

A real Chromium opens web/index.html from a local server. Its navigator.serial
is fake_serial.js: one USB port whose other end is the simulator, so every
`@MB` line the page writes is read by the real firmware code and the page
reads the firmware's real answers. Nothing here can reach a real board.

Needs the `playwright` package and its Chromium (`playwright install chromium`,
or MBSIM_CHROME=/path/to/chrome).
"""
import functools
import glob
import http.server
import json
import os
import sys
import threading

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(SIM))
sys.path.insert(0, SIM)

from mbsim import BINARY, Buddy  # noqa: E402
from mock_internet import MockInternet  # noqa: E402

playwright_api = pytest.importorskip('playwright.sync_api', reason='the browser tests need the playwright package')
OUT = os.environ.get('MBSIM_OUT', os.path.join(SIM, 'out', 'web'))


def pytest_configure(config):
    if not os.path.exists(BINARY):
        raise pytest.UsageError('no simulator at %s: run `make` in tools/sim first' % BINARY)
    os.makedirs(OUT, exist_ok=True)


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


@pytest.fixture(scope='session')
def site():
    """web/ on a local port."""
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Quiet, directory=os.path.join(ROOT, 'web')))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    yield 'http://127.0.0.1:%d' % server.server_address[1]
    server.shutdown()


def chrome():
    path = os.environ.get('MBSIM_CHROME')
    if path:
        return path
    try:
        import playwright
        wanted = json.load(open(os.path.join(os.path.dirname(playwright.__file__), 'driver', 'package', 'browsers.json')))
        revision = next(b['revision'] for b in wanted['browsers'] if b['name'] == 'chromium')
        cache = os.environ.get('PLAYWRIGHT_BROWSERS_PATH') or os.path.expanduser('~/.cache/ms-playwright')
        if os.path.isdir(os.path.join(cache, 'chromium-' + revision)):
            return None                      # the one this playwright asks for is installed
        found = sorted(glob.glob(os.path.join(cache, 'chromium-*', 'chrome-linux*', 'chrome')))
        return found[-1] if found else None  # another revision is: use it
    except Exception:
        return None


@pytest.fixture(scope='session')
def browser():
    with playwright_api.sync_playwright() as p:
        if os.environ.get('MBSIM_BROWSER') == 'firefox':   # the app's editor (test_app.py) has to work there too
            b = p.firefox.launch(headless=True)
        else:
            b = p.chromium.launch(executable_path=chrome(), headless=True)
        yield b
        b.close()


class Desk:
    """A browser tab on the installer and the simulated Buddy plugged into it."""

    def __init__(self, page, buddy, site):
        self.page, self.buddy, self.site = page, buddy, site
        self.sent = 0                 # how much of buddy.log the page has been given
        self.speed = 100              # simulated ms per 40 ms tick of the page
        self.plugged = True
        self.errors = []
        page.on('pageerror', lambda e: self.errors.append(str(e)))
        page.on('console', lambda m: self.errors.append(m.text) if m.type == 'error' and 'favicon' not in m.text
                and 'Failed to load resource' not in m.text and 'HTTP 404' not in m.text else None)
        self.outside = []             # requests the page made to the internet (all answered here, none leaves)

    # ── the cable ─────────────────────────────────────────────
    def _new_output(self):
        self.buddy._pull_log()
        text, self.sent = self.buddy.log[self.sent:], len(self.buddy.log)
        return text.encode().hex() if self.plugged else ''

    def write(self, data):
        if not self.plugged:
            return ''
        self.buddy.cmd('serial_in ' + data, expect_restart=True)
        self.buddy.run_ms(20, expect_restart=True)
        return self._new_output()

    def tick(self):
        self.buddy.run_ms(self.speed, expect_restart=True)
        return self._new_output()

    # ── the page ──────────────────────────────────────────────
    def _internet(self, route):
        url = route.request.url
        if url.startswith(self.site):
            return route.fallback()
        self.outside.append(url)
        if 'geocoding-api.open-meteo.com' in url:
            return route.fulfill(json={'results': [{'name': 'Cairo', 'country': 'Egypt', 'admin1': 'Cairo', 'latitude': 30.0626,
                                                    'longitude': 31.2497, 'timezone': 'Africa/Cairo'}]})
        if 'api.github.com/users/' in url:
            return route.fulfill(json={'login': url.rsplit('/', 1)[-1], 'followers': 42, 'public_repos': 7})
        if 'coinbase.com/products' in url:
            return route.fulfill(json=[{'id': 'BTC-USD', 'base_currency': 'BTC', 'quote_currency': 'USD', 'status': 'online',
                                        'trading_disabled': False}])
        return route.fulfill(status=404, body='')

    def open(self, query='', releases=None, serial=True):
        """Loads the installer. `releases`: the firmware list it finds (None: there is none, a 404)."""
        self.page.add_init_script(path=os.path.join(HERE, 'fake_serial.js'))
        if not serial:                         # a browser without Web Serial (Firefox, Safari, a phone)
            self.page.add_init_script('delete Navigator.prototype.serial')
        self.page.route('**/*', self._internet)
        self.page.expose_function('mbsimWrite', self.write)
        self.page.expose_function('mbsimTick', self.tick)
        if releases is not None:
            self.page.route('**/firmware/releases.json', lambda route: route.fulfill(json={'format': 1, 'releases': releases}))
        self.sent = len(self.buddy.log)        # what it printed before the cable went in is gone
        self.page.goto(self.site + '/index.html' + query)
        self.page.wait_for_selector('h1')
        return self

    def text(self, path, *args):
        """A text of the installer (web/js/strings.js), e.g. text('home.change'); with args when it is a function."""
        value = self.page.evaluate(
            """async ([path, args]) => {
                 const { S } = await import('/js/strings.js');
                 const value = path.split('.').reduce((o, k) => (o == null ? o : o[k]), S);
                 return typeof value === 'function' ? value(...args) : value ?? null;
               }""", [path, list(args)])
        assert value is not None, 'strings.js has no text S.' + path
        return value

    def title(self):
        return self.page.inner_text('h1')

    def wait_title(self, path, timeout=30000):
        want = self.text(path)
        try:
            self.page.wait_for_function('want => document.querySelector("h1") && document.querySelector("h1").innerText === want',
                                        arg=want, timeout=timeout)
        except playwright_api.TimeoutError:
            self.shot('timeout')
            raise AssertionError('the screen "%s" did not come; the page shows "%s": %s' % (
                want, self.title(), self.page.inner_text('main')[:600]))

    def click(self, path, *args):
        self.page.get_by_role('button').filter(has_text=self.text(path, *args)).first.click()

    def connect(self):
        self.click('connect')
        self.wait_title('titles.home')

    def wrote(self):
        """Everything the page wrote to the port."""
        return self.page.evaluate('window.__mbsim.written')

    def shot(self, name):
        self.page.screenshot(path=os.path.join(OUT, name + '.png'), full_page=True)


@pytest.fixture
def make_desk(browser, site):
    made = []

    def make(query='', releases=None, open_page=True, viewport=None, boot_s=14, serial=True, **buddy_options):
        mock = MockInternet().start()
        buddy = Buddy(internet=mock, **buddy_options)
        buddy.run(boot_s)                           # 14 s: booted, on Wi-Fi, clock set
        context = browser.new_context(viewport=viewport or {'width': 1100, 'height': 900}, accept_downloads=True)
        desk = Desk(context.new_page(), buddy, site)
        made.append((desk, context, mock))
        return desk.open(query, releases, serial) if open_page else desk

    yield make
    for desk, context, mock in made:
        context.close()
        desk.buddy.close()
        mock.stop()


@pytest.fixture
def desk(make_desk):
    return make_desk()
