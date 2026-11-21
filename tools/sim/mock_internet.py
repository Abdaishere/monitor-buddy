"""The internet as the simulated Buddy sees it: weather, GitHub, stocks, coins.

The simulator sends every HTTP request of the firmware here as
GET /<host><path>. Answers are fixed data, so a run is repeatable; a test can
change them (`mock.weather['temperature_2m'] = 41`) or break a host
(`mock.fail['api.open-meteo.com'] = 500`, `'timeout'`, `'cut'`, `'garbage'`).
"""
import json
import threading
from datetime import date, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse


class MockInternet:
    def __init__(self, today=date(2026, 1, 5)):
        self.today = today
        self.weather = {'temperature_2m': 27.4, 'relative_humidity_2m': 48, 'weather_code': 1,
                        'wind_speed_10m': 13.2, 'is_day': 1}
        self.github = {'followers': 128, 'public_repos': 31, 'stars': [12, 40, 3, 0, 7], 'streak': 9, 'today': 4}
        self.stocks = {'AAPL': {'c': 231.42, 'd': 1.15, 'dp': 0.4994, 'h': 233.1, 'l': 229.8, 'o': 230.2, 'pc': 230.27, 't': 1767600000}}
        self.coins = {'BTC-USD': {'open': '91250.10', 'high': '93890.00', 'low': '90811.55', 'last': '93120.42', 'volume': '8123.4'}}
        self.fail = {}          # host -> status code, 'timeout', 'cut' or 'garbage'
        self.latency_ms = {}    # host -> the answer's delay in simulated milliseconds
        self.requests = []      # (host, path) of every request, in order
        self._server = None

    # ── answers ───────────────────────────────────────────────
    def _contributions(self):
        """A year of GitHub's contribution calendar, ending today, as its HTML."""
        cells = []
        for back in range(370, -1, -1):
            day = self.today - timedelta(days=back)
            if back == 0:
                count = self.github['today']
            elif back < self.github['streak'] + (0 if self.github['today'] else 1):
                count = 1 + (back * 7) % 5
            elif back == self.github['streak'] + (0 if self.github['today'] else 1):
                count = 0
            else:
                count = (back * 5) % 4 if back % 3 else 0
            ident = 'contribution-day-component-%d-%d' % (day.weekday(), back)
            cells.append('<td tabindex="0" data-ix="%d" style="width: 11px" data-date="%s" id="%s" data-level="%d" '
                         'role="gridcell" class="ContributionCalendar-day"></td>\n'
                         '<tool-tip id="tooltip-%d" for="%s" popover="manual" data-type="label" '
                         'class="sr-only position-absolute">%s on %s.</tool-tip>\n'
                         % (back, day.isoformat(), ident, min(count, 4), back, ident,
                            'No contributions' if not count else '%d contribution%s' % (count, '' if count == 1 else 's'),
                            day.strftime('%B %-d')))
        return ('<table class="ContributionCalendar-grid js-calendar-graph-table">\n<tbody>\n<tr style="height: 10px">\n'
                + ''.join(cells) + '</tr>\n</tbody>\n</table>\n')

    def answer(self, host, path, query):
        """(status, content type, body) for one request."""
        if host == 'api.open-meteo.com':
            return 200, 'application/json', json.dumps({'latitude': 25.2, 'longitude': 55.27, 'current': dict(self.weather, time='2026-01-05T12:00')})
        if host == 'api.github.com' and path.endswith('/repos'):
            return 200, 'application/json', json.dumps([{'name': 'repo%d' % i, 'stargazers_count': stars, 'fork': False}
                                                        for i, stars in enumerate(self.github['stars'])])
        if host == 'api.github.com':
            return 200, 'application/json', json.dumps({'login': path.rsplit('/', 1)[-1], 'followers': self.github['followers'],
                                                        'public_repos': self.github['public_repos']})
        if host == 'github.com' and path.endswith('/contributions'):
            return 200, 'text/html', self._contributions()
        if host == 'finnhub.io':
            symbol = query.get('symbol', [''])[0]
            quote = self.stocks.get(symbol.replace('COINBASE:', ''))
            return 200, 'application/json', json.dumps(quote or {'c': 0, 'd': None, 'dp': None, 'h': 0, 'l': 0, 'o': 0, 'pc': 0, 't': 0})
        if host == 'api.exchange.coinbase.com':
            parts = path.strip('/').split('/')   # products/PAIR/stats or /candles
            pair = parts[1] if len(parts) > 1 else ''
            if pair not in self.coins:
                return 404, 'application/json', json.dumps({'message': 'NotFound'})
            if parts[-1] == 'candles':
                last = float(self.coins[pair]['last'])
                return 200, 'application/json', json.dumps([[1767600000 - 3600 * i, last * 0.99, last * 1.01, last, last * (1 - 0.002 * i), 5.0]
                                                            for i in range(24)])
            return 200, 'application/json', json.dumps(self.coins[pair])
        return 404, 'text/plain', 'the mock internet has no %s' % host

    # ── server ────────────────────────────────────────────────
    def start(self):
        mock = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = 'HTTP/1.0'

            def log_message(self, *args):
                pass

            def do_GET(self):
                url = urlparse(self.path)
                host, _, path = url.path.lstrip('/').partition('/')
                path = '/' + path
                mock.requests.append((host, path + ('?' + url.query if url.query else '')))
                status, kind, body = mock.answer(host, path, parse_qs(url.query))
                fault = mock.fail.get(host)
                extra = {}
                if isinstance(fault, int):
                    status, body = fault, '{"message":"mock failure"}'
                elif fault == 'garbage':
                    body = '<html>502 Bad Gateway</html>'
                elif fault in ('timeout', 'cut'):
                    extra['X-Sim-' + fault.capitalize()] = '1'
                if host in mock.latency_ms:
                    extra['X-Sim-Latency-Ms'] = str(mock.latency_ms[host])
                data = body.encode()
                self.send_response(status)
                self.send_header('Content-Type', kind)
                self.send_header('Content-Length', str(len(data)))
                for name, value in extra.items():
                    self.send_header(name, value)
                self.end_headers()
                self.wfile.write(data)

        self._server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=self._server.serve_forever, daemon=True).start()
        return self

    @property
    def address(self):
        return '127.0.0.1:%d' % self._server.server_address[1]

    def stop(self):
        if self._server:
            self._server.shutdown()
            self._server.server_close()
            self._server = None


if __name__ == '__main__':
    import time
    mock = MockInternet().start()
    print('mock internet on', mock.address, flush=True)
    while True:
        time.sleep(3600)
