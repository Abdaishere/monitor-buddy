"""Tests of the app (tools/buddy_app.py): python3 -m unittest discover -s tools

The server runs on 127.0.0.1 with a pretend Buddy in a temporary home folder.
"""
import base64
import io
import json
import os
import threading
import time
import unittest
import urllib.error
import urllib.request
from unittest import mock

import buddy_app
import buddy_keys
from test_buddy_setup import FakeBoard, Home

TOOLS = os.path.dirname(os.path.abspath(__file__))


class FakePut:
    """tools/buddy_put.py with a Buddy that holds what it was sent."""

    class Refused(Exception):
        pass

    def __init__(self):
        self.gifs, self.pictures, self.problem = {'pong.gif': 1000}, [], None

    def buddy(self):
        if self.problem:
            raise self.Refused(self.problem)
        return 'cipher', '10.0.0.5'

    def animations(self, cipher, address):
        return {'gifs': sorted([name, size] for name, size in self.gifs.items()), 'free_kb': 500}

    def put(self, cipher, address, name, data):
        self.gifs[name] = len(data)

    def remove(self, cipher, address, name):
        self.gifs.pop(name)

    def picture(self, cipher, address, pixels):
        self.pictures.append(pixels)


class AppCase(Home):
    def setUp(self):
        super().setUp()
        self.board = FakeBoard()
        self.said = []
        self.say_problem = None

        def make(**options):
            setup = buddy_app.Recorder(home=self.home, repo=os.path.dirname(TOOLS), **options)
            setup.address_wait_s = 0
            started = []

            def run(*cmd, timeout=30):
                if cmd[:2] == ('systemctl', '--user') and cmd[2] in ('start', 'restart'):
                    started.append(1)
                return (0, 'active' if started and cmd[2] == 'is-active' else '') if cmd[0] == 'systemctl' else (127, '')

            setup.run = run
            setup.which = lambda name: '/usr/bin/' + name if name in ('systemctl', 'loginctl', 'fish') else None
            setup.find_port = lambda: '/dev/ttyFAKE' if self.board else None
            setup.open_board = lambda port: self.board
            setup.ping = lambda: bool(self.board and self.board.link)
            setup.groups = lambda: ({'dialout'}, True)
            return setup

        def say(text):
            self.said.append(text)
            return self.say_problem

        self.fetched = []

        def fetch(url):
            self.fetched.append(url)
            return json.dumps({'results': [
                {'name': 'Lisbon', 'admin1': 'Lisbon District', 'country': 'Portugal', 'latitude': 38.71667, 'longitude': -9.13333, 'timezone': 'Europe/Lisbon'},
                {'name': 'Nowhere', 'latitude': 1, 'longitude': 2, 'timezone': 'Mars/Olympus'}]}).encode()

        self.data = os.path.join(self.home, '.config', 'monitor-buddy')   # where the earlier settings go
        self.put = FakePut()
        self.app = buddy_app.App(make_setup=make, say=say, fetch=fetch, linux=True, data=self.data, put=self.put)
        self.server, self.token, self.state = buddy_app.serve(self.app)
        self.port = self.server.server_address[1]
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)

    def call(self, path, body=None, token=None, host=None):
        request = urllib.request.Request(f'http://127.0.0.1:{self.port}{path}', data=None if body is None else json.dumps(body).encode())
        request.add_header('X-Buddy-Token', self.token if token is None else token)
        if host:
            request.add_header('Host', host)
        try:
            with urllib.request.urlopen(request, timeout=10) as answer:
                return answer.status, answer.read()
        except urllib.error.HTTPError as error:
            with error:
                return error.code, error.read()

    def api(self, path, body=None):
        code, data = self.call(path, body)
        self.assertEqual(code, 200, data)
        return json.loads(data)

    def finish(self):
        for _ in range(200):
            progress = self.api('/api/progress')
            if not progress['running']:
                return progress
            time.sleep(0.05)
        self.fail('setup did not finish')


class Setup(AppCase):
    def test_status_changes_nothing(self):
        status = self.api('/api/status')
        self.assertEqual(status['missing'], [])
        self.assertEqual(status['answers'], {'odoo': False, 'claude': False, 'fish': False, 'firewall': False})
        self.assertFalse(status['done_before'])
        self.assertEqual(os.listdir(self.home), [])
        self.assertEqual(self.board.writes, 0)

    def test_a_run_pairs_the_board_and_reports_steps(self):
        self.assertEqual(self.api('/api/setup', {'fish': True}), {'ok': True})
        progress = self.finish()
        self.assertIsNone(progress['failed'])
        self.assertEqual(self.board.writes, 1)
        self.assertEqual(self.board.link, buddy_keys.for_board()['link_key'])
        self.assertEqual(len([e for e in progress['events'] if e['kind'] == 'step']), 8)
        self.assertTrue(any('took the keys' in e['text'] for e in progress['events']))
        self.assertTrue(any('Wi-Fi link works' in line for line in progress['works']))
        self.assertEqual(progress['left'], [])
        self.assertTrue(os.path.exists(os.path.join(self.home, '.config', 'fish', 'conf.d', 'buddy.fish')))
        self.assertTrue(os.path.exists(os.path.join(self.home, '.local', 'share', 'applications', 'monitor-buddy.desktop')))

    def test_tick_boxes_are_the_answers_and_are_remembered(self):
        os.makedirs(os.path.join(self.home, '.claude'))
        self.api('/api/setup', {'claude': True, 'fish': False})
        self.finish()
        self.assertTrue(os.path.exists(os.path.join(self.home, '.claude', 'settings.json')))
        self.assertFalse(os.path.exists(os.path.join(self.home, '.config', 'fish', 'conf.d', 'buddy.fish')))
        status = self.api('/api/status')
        self.assertEqual(status['answers'], {'odoo': False, 'claude': True, 'fish': False, 'firewall': False})
        self.assertTrue(status['done_before'])

    def test_a_second_run_resumes_and_keeps_the_keys(self):
        self.board, board = None, self.board
        self.api('/api/setup', {})
        progress = self.finish()
        self.assertTrue(any('No Buddy found' in item['text'] for item in progress['left']))
        keys = self.read(self.link_file), self.read(self.signing_file)
        self.board = board
        self.api('/api/setup', {})
        progress = self.finish()
        self.assertEqual(progress['left'], [])
        self.assertEqual((self.read(self.link_file), self.read(self.signing_file)), keys)
        self.assertEqual(board.writes, 1)

    def test_a_board_with_other_keys_needs_the_tick(self):
        self.board.link, self.board.update = 'ee' * 32, 'OTHER'
        self.api('/api/setup', {})
        progress = self.finish()
        self.assertEqual(self.board.writes, 0)
        self.assertTrue(any('another computer' in item['text'] for item in progress['left']))
        self.api('/api/setup', {'repair': True})
        self.finish()
        self.assertEqual(self.board.writes, 1)

    def test_no_answer_holds_a_key(self):
        self.api('/api/setup', {'fish': True})
        text = json.dumps(self.finish()) + json.dumps(self.api('/api/status'))
        self.assertNotIn(self.read(self.link_file).decode().strip(), text)
        self.assertNotIn('PRIVATE KEY', text)
        self.assertNotIn(buddy_keys.for_board()['update_key'].split('\n')[1], text)

    def test_an_unexpected_error_is_shown_not_hidden(self):
        def explode(port):
            raise RuntimeError('boom')
        make = self.app.make_setup

        def broken(**options):
            setup = make(**options)
            setup.open_board = explode
            return setup

        self.app.make_setup = broken
        self.api('/api/setup', {})
        progress = self.finish()
        self.assertIn('Something unexpected went wrong', progress['failed'])


class Packages(AppCase):
    def test_missing_parts_are_named_and_can_be_installed(self):
        self.app.missing_packages = lambda: ['python3-serial']
        status = self.api('/api/status')
        self.assertEqual(status['missing'], ['python3-serial'])
        self.assertEqual(status['command'], 'sudo apt install python3-serial')

    def test_a_closed_password_window_is_explained(self):
        self.app.missing_packages = lambda: ['python3-serial']
        self.app._run = lambda *cmd: 126
        import shutil
        if not (shutil.which('pkexec') and shutil.which('apt-get')):
            self.assertIn('no graphical installer', self.api('/api/install', {})['error'])
        else:
            self.assertIn('password window was closed', self.api('/api/install', {})['error'])


class Settings(AppCase):
    """Make it yours: the Buddy's settings over the cable."""

    def setUp(self):
        super().setUp()
        self.board.config = {'name': 'Sam', 'face': 'classic', 'work': {'start': 9, 'end': 17, 'lunch': 12}, 'theme': 2,
                             'stock_key': 'secret-finnhub-key', 'lat': '1.3', 'water': False}
        self.board.sets = []
        self.board.get_config = lambda: json.loads(json.dumps(self.board.config))

        def set_config(patch, wifi=None):
            self.board.sets.append(patch)
            self.board.wifi_sent = wifi
            if patch.get('face') == 'dog':
                return False, {'face': 'must be "classic", "cat", "robot" or "minimal"'}
            self.board.config.update(patch)
            return True, {}

        self.board.set_config = set_config

    def test_only_the_editable_settings_are_shown(self):
        answer = self.api('/api/settings')
        self.assertTrue(answer['ok'])
        self.assertEqual(set(answer['config']), {'name', 'face', 'work', 'theme', 'water', 'lat'})
        self.assertNotIn('secret-finnhub-key', json.dumps(answer))
        self.assertEqual(answer['has'], {'stock_key': True}, 'the page learns only that there is a key')
        self.assertEqual(answer['wifi'], {'ssid': 'home', 'connected': True})

    def test_a_change_reaches_the_buddy(self):
        self.assertTrue(self.api('/api/settings', {'config': {'face': 'cat', 'water': True}})['ok'])
        self.assertEqual(self.board.sets, [{'face': 'cat', 'water': True}])
        self.assertEqual(self.api('/api/settings')['config']['face'], 'cat')

    def test_other_keys_are_never_sent(self):
        self.api('/api/settings', {'config': {'face': 'robot', 'link_key': 'ab' * 32, 'v': 9, 'ticker': 'X'}})
        self.assertEqual(self.board.sets, [{'face': 'robot'}])
        self.assertFalse(self.api('/api/settings', {'config': {'update_key': 'x'}})['ok'])
        self.assertEqual(len(self.board.sets), 1)

    def test_a_new_finnhub_key_can_be_written(self):
        self.api('/api/settings', {'config': {'stock_key': 'newkey'}})
        self.assertEqual(self.board.sets, [{'stock_key': 'newkey'}])

    def test_wifi_goes_over_the_cable_and_is_never_echoed(self):
        answer = self.api('/api/settings', {'config': {}, 'wifi': {'ssid': 'CafeNet', 'password': 'cafepass99'}})
        self.assertTrue(answer['ok'])
        self.assertNotIn('cafepass99', json.dumps(os.listdir(self.data)) + self.kept_text())
        self.assertEqual(self.board.wifi_sent, {'ssid': 'CafeNet', 'password': 'cafepass99'})
        self.assertNotIn('cafepass99', json.dumps(self.api('/api/settings')))
        self.assertFalse(self.api('/api/settings', {'wifi': {'ssid': '', 'password': 'x'}})['ok'], 'no name, no change')

    def test_the_city_search_gives_what_the_buddy_needs(self):
        answer = self.api('/api/city?q=Lisbon')
        self.assertEqual(answer['places'], [{'label': 'Lisbon, Lisbon District, Portugal', 'lat': '38.7167', 'lon': '-9.1333',
                                             'tz_name': 'Europe/Lisbon', 'tz_rule': 'WET0WEST,M3.5.0/1,M10.5.0'}])
        self.assertTrue(self.fetched[0].endswith('name=Lisbon'))
        self.assertEqual(self.api('/api/city?q=')['places'], [])
        self.assertEqual(len(self.fetched), 1, 'an empty search asks nobody')

    def test_a_city_search_without_internet_is_explained(self):
        def offline(url):
            raise OSError('no route')
        self.app._fetch = offline
        self.assertIn('did not answer', self.api('/api/city?q=Lisbon')['error'])

    def test_what_the_buddy_refuses_is_said_per_field(self):
        answer = self.api('/api/settings', {'config': {'face': 'dog'}})
        self.assertFalse(answer['ok'])
        self.assertIn('face', answer['errors'])
        self.assertEqual(self.board.config['face'], 'classic')

    def test_without_the_buddy_it_says_to_plug_it_in(self):
        self.board = None
        answer = self.api('/api/settings')
        self.assertEqual((answer['ok'], answer['error']), (False, buddy_app.NO_BOARD))

    def test_a_silent_port_is_explained(self):
        def silent():
            raise TimeoutError('no reply to GET')
        self.board.get_config = silent
        self.assertIn('did not answer on the cable', self.api('/api/settings')['error'])

    def test_the_face_pictures_are_the_installers(self):
        for name in ('classic', 'cat', 'robot', 'minimal'):
            code, data = self.call(f'/img/face-{name}.png', token='')
            self.assertEqual(code, 200)
            with open(os.path.join(TOOLS, '..', 'web', 'img', f'face-{name}.png'), 'rb') as file:
                self.assertEqual(data, file.read(), name)

    def test_the_page_offers_every_editable_setting(self):
        page = self.call('/', token='')[1].decode()
        together = {'lat': 'city', 'lon': 'city', 'tz_name': 'city', 'tz_rule': 'city',   # one field sets these four
                    'pages': 'round', 'more_pages': 'round', 'page_order': 'round'}       # and the list of pages these three
        for key in buddy_app.EDITABLE + buddy_app.WRITE_ONLY:
            self.assertIn(f'["{together.get(key, key)}", ', page, key)


    def kept_text(self):
        """Everything the backups folder holds, as one text."""
        folder = os.path.join(self.data, 'backups')
        text = ''
        for name in sorted(os.listdir(folder)) if os.path.isdir(folder) else []:
            with open(os.path.join(folder, name)) as file:
                text += file.read()
        return text

    # ── Apply can be undone ───────────────────────────────────────────────
    def test_what_the_buddy_held_is_kept_before_a_change(self):
        answer = self.api('/api/settings', {'config': {'face': 'cat'}})
        self.assertEqual(len(answer['backups']), 1)
        kept = self.api('/api/backups')['backups']
        self.assertEqual([entry['id'] for entry in kept], [answer['backups'][0]['id']])
        text = self.kept_text()
        self.assertIn('"face": "classic"', text)
        self.assertNotIn('secret-finnhub-key', text, 'a key is never written to this computer')
        self.assertNotIn('stock_key', text)

    def test_undo_puts_the_earlier_settings_back(self):
        first = self.api('/api/settings', {'config': {'face': 'cat', 'name': 'Alex'}})['backups'][0]['id']
        self.assertEqual(self.board.config['face'], 'cat')
        answer = self.api('/api/restore', {'id': first})
        self.assertTrue(answer['ok'])
        self.assertEqual(self.board.sets[-1], {'face': 'classic', 'name': 'Sam'}, 'only what differs goes over the cable')
        self.assertEqual((self.board.config['face'], self.board.config['name']), ('classic', 'Sam'))
        self.assertEqual(self.board.config['stock_key'], 'secret-finnhub-key', 'the key is not touched')
        self.assertEqual(answer['config']['face'], 'classic')
        self.assertEqual(len(self.api('/api/backups')['backups']), 2, 'the undone settings are kept too: an undo can be undone')

    def test_restoring_what_the_buddy_already_holds_changes_nothing(self):
        first = self.api('/api/settings', {'config': {'face': 'cat'}})['backups'][0]['id']
        self.api('/api/settings', {'config': {'face': 'classic'}})
        sets = len(self.board.sets)
        answer = self.api('/api/restore', {'id': first})
        self.assertEqual((answer['ok'], answer.get('same')), (True, True))
        self.assertEqual(len(self.board.sets), sets)

    def test_an_unknown_backup_is_refused(self):
        for ident in ('', '../../etc/passwd', '20990101-000000'):
            self.assertFalse(self.api('/api/restore', {'id': ident})['ok'], ident)
        self.assertEqual(self.board.sets, [])

    def test_the_same_settings_are_kept_once(self):
        self.api('/api/settings', {'config': {'face': 'dog'}})      # refused by the Buddy: it still holds the same
        self.api('/api/settings', {'config': {'face': 'dog'}})
        self.assertEqual(len(self.api('/api/backups')['backups']), 1)

    def test_only_so_many_are_kept(self):
        with mock.patch.object(buddy_app, 'BACKUPS_KEPT', 3):
            for theme in (0, 1, 3, 4, 0, 1):
                self.api('/api/settings', {'config': {'theme': theme}})
        self.assertEqual(len(self.api('/api/backups')['backups']), 3)

    def test_a_setting_the_firmware_lacks_is_not_sent(self):
        """An older Buddy would skip page_order without a word: the app says it needs the update instead."""
        answer = self.api('/api/settings', {'config': {'page_order': ['clock', 'face'], 'face': 'cat'}})
        self.assertFalse(answer['ok'])
        self.assertEqual(answer['needs_update'], ['page_order'])
        self.assertEqual(self.board.sets, [])
        self.board.config.update(page_order=[], more_pages={'game': True})
        self.assertTrue(self.api('/api/settings', {'config': {'page_order': ['clock', 'face'], 'more_pages': {'game': False}}})['ok'])
        self.assertEqual(self.board.sets, [{'page_order': ['clock', 'face'], 'more_pages': {'game': False}}])

    # ── the preview ───────────────────────────────────────────────────────
    def test_the_preview_is_served_to_the_page(self):
        for path, kind, start in (('/sim/mbsim.wasm', 'application/wasm', b'\0asm'), ('/sim/mbsim.js', 'text/javascript', None),
                                  ('/sim/buddy-sim.js', 'text/javascript', None)):
            request = urllib.request.Request(f'http://127.0.0.1:{self.port}{path}')
            with urllib.request.urlopen(request, timeout=10) as answer:
                data = answer.read()
                self.assertEqual(answer.headers['Content-Type'], kind)
                self.assertIn("'wasm-unsafe-eval'", answer.headers['Content-Security-Policy'])
            self.assertGreater(len(data), 1000, path)
            if start:
                self.assertTrue(data.startswith(start))
        self.assertEqual(self.call('/sim/../buddy_app.py', token='')[0], 403)

    def test_the_page_draws_the_round_with_every_page_of_the_firmware(self):
        """The page's list of pages and their ids are the firmware's (lib/settings, src/main.cpp)."""
        import re
        page = self.call('/', token='')[1].decode()
        with open(os.path.join(TOOLS, '..', 'lib', 'settings', 'settings.cpp')) as file:
            source = file.read()
        keys = re.findall(r'"(\w+)"', re.search(r'SETTINGS_ROUND_KEYS\[SETTINGS_ROUND_COUNT\] = \{(.*?)\};', source, re.S).group(1))
        usual = [int(n) for n in re.search(r'SETTINGS_ROUND_DEFAULT\[SETTINGS_ROUND_COUNT\] = \{(.*?)\};', source, re.S).group(1).split(',')]
        table = re.search(r'const PAGES = \{(.*?)\n\};', page, re.S).group(1)
        rows = re.findall(r'^  (\w+): \[(\d+), ', table, re.M)
        self.assertEqual([name for name, _ in rows], [keys[i] for i in usual], 'the page lists them in the usual order')
        self.assertEqual({name: int(ident) for name, ident in rows}, {name: i for i, name in enumerate(keys)})


class OtherSystems(AppCase):
    """Windows (and anything that is not Linux): no helper, the Buddy on a serial port."""

    def setUp(self):
        super().setUp()
        self.board.config = {'name': 'Sam', 'face': 'classic', 'theme': 2, 'stock_key': ''}
        self.board.get_config = lambda: json.loads(json.dumps(self.board.config))
        self.board.set_config = lambda patch, wifi=None: (self.board.config.update(patch), (True, {}))[1]
        self.opened = []
        self.ports = ['COM3', 'COM10']

        def open_board(port):
            self.opened.append(port)
            if port == 'COM3':
                raise OSError('could not open port COM3: access denied')
            return self.board

        def no_setup(**options):
            raise AssertionError('the Linux setup must not run here')

        self.app.linux, self.app.make_setup = False, no_setup
        self.app._ports, self.app._open_board = (lambda: self.ports), open_board
        self.enterContext(mock.patch.object(self.app, 'missing_packages', lambda: []))

    def test_the_status_says_there_is_no_helper(self):
        status = self.api('/api/status')
        self.assertEqual((status['helper'], status['missing'], status['left']), (False, [], []))
        self.assertFalse(self.api('/api/setup', {})['ok'])

    def test_the_buddy_is_found_among_the_ports(self):
        answer = self.api('/api/settings')
        self.assertTrue(answer['ok'])
        self.assertEqual(self.opened, ['COM3', 'COM10'], 'a port that is busy or not a Buddy is passed over')
        self.assertEqual((answer['config']['name'], answer['fw']), ('Sam', '1.0.0'))

    def test_a_named_port_is_the_only_one_tried(self):
        self.app.port = 'COM10'
        self.assertTrue(self.api('/api/settings', {'config': {'face': 'robot'}})['ok'])
        self.assertEqual((self.opened, self.board.config['face']), (['COM10'], 'robot'))

    def test_no_port_says_to_plug_it_in(self):
        self.ports = []
        self.assertEqual(self.api('/api/settings')['error'], buddy_app.NO_BOARD)

    def test_a_port_that_never_answers_is_explained(self):
        self.ports = ['COM3']
        self.assertIn('did not answer on the cable (COM3)', self.api('/api/settings')['error'])

    def test_say_explains_it_is_the_linux_helpers(self):
        self.app._say = None
        self.assertIn('Wi-Fi', self.api('/api/say', {'text': 'hi'})['error'])

    def test_a_running_app_is_found_by_its_port_not_by_a_signal(self):
        """os.kill(pid, 0) ends a process on Windows: the second start asks the port instead."""
        run_file = os.path.join(self.home, 'app.json')
        with mock.patch.object(buddy_app, 'RUN_FILE', run_file), mock.patch.object(os, 'kill', side_effect=AssertionError('no signals')):
            with open(run_file, 'w') as file:
                json.dump({'pid': 1, 'url': f'http://127.0.0.1:{self.port}/#t=x'}, file)
            self.assertEqual(buddy_app.running(), f'http://127.0.0.1:{self.port}/#t=x')
            with open(run_file, 'w') as file:
                json.dump({'pid': 1, 'url': 'http://127.0.0.1:9/#t=x'}, file)
            self.assertIsNone(buddy_app.running())


class Say(AppCase):
    def test_a_line_goes_to_the_buddy(self):
        self.assertEqual(self.api('/api/say', {'text': '  tea   is ready '}), {'ok': True, 'error': None, 'text': 'tea is ready'})
        self.assertEqual(self.said, ['tea is ready'])

    def test_an_empty_line_is_not_sent(self):
        self.assertFalse(self.api('/api/say', {'text': '   '})['ok'])
        self.assertEqual(self.said, [])

    def test_a_long_line_is_cut(self):
        self.api('/api/say', {'text': 'x' * 500})
        self.assertEqual(len(self.said[0]), buddy_app.SAY_MAX)

    def test_a_problem_is_passed_on(self):
        self.say_problem = 'The Buddy does not answer at 10.0.0.5.'
        answer = self.api('/api/say', {'text': 'hi'})
        self.assertEqual((answer['ok'], answer['error']), (False, self.say_problem))


class Pictures(AppCase):
    def gif(self):
        from PIL import Image
        frames = [Image.new('RGB', (400, 300), colour) for colour in ('red', 'blue')]
        out = io.BytesIO()
        frames[0].save(out, 'GIF', save_all=True, append_images=frames[1:], duration=100, loop=0)
        return base64.b64encode(out.getvalue()).decode()

    def test_the_animations_are_listed(self):
        self.assertEqual(self.api('/api/gifs'), {'ok': True, 'gifs': [['pong.gif', 1000]], 'free_kb': 500})

    def test_a_gif_is_made_to_fit_and_sent_under_a_name_the_buddy_takes(self):
        answer = self.api('/api/gifs/add', {'name': '../My cat!.GIF', 'data': self.gif()})
        self.assertTrue(answer['ok'], answer)
        self.assertEqual(answer['added'], 'My-cat-.gif')
        self.assertIn(['My-cat-.gif', self.put.gifs['My-cat-.gif']], answer['gifs'])

    def test_what_is_no_picture_is_not_sent(self):
        answer = self.api('/api/gifs/add', {'name': 'x.gif', 'data': base64.b64encode(b'not a gif').decode()})
        self.assertEqual((answer['ok'], answer['error']), (False, 'x.gif is not a picture this computer can read.'))
        self.assertEqual(self.api('/api/gifs/add', {'name': 'x.gif', 'data': '%%%'})['error'], 'bad request')
        self.assertEqual(list(self.put.gifs), ['pong.gif'])

    def test_a_gif_is_removed(self):
        self.assertEqual(self.api('/api/gifs/remove', {'name': 'pong.gif'})['gifs'], [])

    def test_the_picture_goes_whole_or_the_github_one_comes_back(self):
        whole = bytes(range(256)) * (88 * 88 * 2 // 256) + bytes(88 * 88 * 2 % 256)
        self.assertEqual(self.api('/api/picture', {'data': base64.b64encode(whole[:-2]).decode()})['error'], 'bad request')
        self.assertTrue(self.api('/api/picture', {'data': base64.b64encode(whole).decode()})['ok'])
        self.assertTrue(self.api('/api/picture', {'data': None})['ok'])
        self.assertEqual(self.put.pictures, [whole, b''])

    def test_a_problem_is_passed_on(self):
        self.put.problem = 'The Buddy does not answer at 10.0.0.5.'
        self.assertEqual(self.api('/api/gifs'), {'ok': False, 'error': self.put.problem})

    def test_only_a_gif_may_be_big(self):
        self.assertEqual(self.call('/api/say', {'text': 'x' * 20000})[0], 400)
        self.assertTrue(self.api('/api/gifs/add', {'name': 'big.gif', 'data': self.gif(), 'pad': 'x' * 20000})['ok'])


class Private(AppCase):
    def test_no_token_no_answer(self):
        for path, body in (('/api/status', None), ('/api/progress', None), ('/api/say', {'text': 'hi'}), ('/api/setup', {}),
                           ('/api/gifs', None), ('/api/gifs/remove', {'name': 'pong.gif'}), ('/api/picture', {'data': None})):
            self.assertEqual(self.call(path, body, token='wrong')[0], 403, path)
            self.assertEqual(self.call(path, body, token='')[0], 403, path)
        self.assertEqual(self.said, [])
        self.assertEqual(self.board.writes, 0)
        self.assertEqual((list(self.put.gifs), self.put.pictures), (['pong.gif'], []))

    def test_another_site_cannot_use_it(self):
        self.assertEqual(self.call('/api/say', {'text': 'hi'}, host='evil.example')[0], 403)
        self.assertEqual(self.call('/', host='evil.example')[0], 403)
        self.assertEqual(self.said, [])

    def test_the_page_holds_no_token_and_no_outside_code(self):
        code, page = self.call('/', token='')
        self.assertEqual(code, 200)
        self.assertNotIn(self.token.encode(), page)
        self.assertNotIn(b'<script src', page)
        self.assertNotIn(b'innerHTML', page)

    def test_it_listens_on_this_computer_only(self):
        self.assertEqual(self.server.server_address[0], '127.0.0.1')

    def test_bad_requests_are_refused(self):
        self.assertEqual(self.call('/api/say', ['x'])[0], 400)
        self.assertEqual(self.call('/api/nothing', {})[0], 404)


if __name__ == '__main__':
    unittest.main()


class ChromeHost(unittest.TestCase):
    """tools/buddy_odoo_host.py: the popup's "say" and the Odoo summary, as Chrome sends them."""

    def message(self, data, home):
        import struct
        import subprocess
        import sys
        body = json.dumps(data).encode()
        done = subprocess.run([sys.executable, os.path.join(TOOLS, 'buddy_odoo_host.py')], input=struct.pack('<I', len(body)) + body,
                              capture_output=True, env={**os.environ, 'HOME': home})
        return json.loads(done.stdout[4:]) if done.stdout else done.stderr.decode()

    def test_say_without_a_setup_is_explained(self):
        import tempfile
        with tempfile.TemporaryDirectory() as home:
            answer = self.message({'say': 'tea is ready'}, home)
            self.assertFalse(answer['ok'])
            self.assertIn('not set up', answer['error'])
            self.assertFalse(os.path.exists(os.path.join(home, '.cache', 'monitor-buddy', 'odoo.json')), 'a say is not saved as Odoo data')

    def test_an_empty_say_is_refused(self):
        import tempfile
        with tempfile.TemporaryDirectory() as home:
            self.assertEqual(self.message({'say': '   '}, home), {'ok': False, 'error': 'Type something first.'})

    def test_the_odoo_summary_is_still_saved(self):
        import tempfile
        with tempfile.TemporaryDirectory() as home:
            self.assertEqual(self.message({'h': 2.5, 'o': 3}, home), {'ok': True})
            with open(os.path.join(home, '.cache', 'monitor-buddy', 'odoo.json')) as file:
                self.assertEqual(json.load(file), {'h': 2.5, 'o': 3})

    def test_the_extension_keeps_its_id_and_asks_for_nothing_more(self):
        with open(os.path.join(TOOLS, 'chrome-odoo', 'manifest.json')) as file:
            manifest = json.load(file)
        self.assertTrue(manifest['key'].startswith('MIIBIjANBgkq'))
        self.assertEqual(manifest['permissions'], ['alarms', 'nativeMessaging'])
        self.assertEqual(manifest['host_permissions'], ['https://www.odoo.com/*'])
        self.assertEqual(manifest['action']['default_popup'], 'popup.html')
        with open(os.path.join(TOOLS, 'chrome-odoo', 'popup.html')) as file:
            self.assertNotIn('<script>', file.read(), 'no inline script: Chrome would block it')
