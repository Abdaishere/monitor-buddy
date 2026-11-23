"""Tests of the keys and of the setup command: python3 -m unittest discover -s tools

Everything runs in a temporary home folder with a pretend Buddy, so no real
key, board, service or settings file is touched.
"""
import io
import json
import os
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import buddy_claude_hook
import buddy_flash
import buddy_keys
import buddy_link
import buddy_setup
import mb_serial

TOOLS = os.path.dirname(os.path.abspath(__file__))


class FakeBoard:
    """A Buddy on USB: keeps the keys it is given and shows their fingerprints,
    as the firmware does. `firmware_keys=False` is a firmware from before @MB KEYS."""

    def __init__(self, firmware_keys=True, takes_keys=True, wifi=None):
        self.link, self.update, self.writes = None, None, 0
        self.firmware_keys, self.takes_keys = firmware_keys, takes_keys
        self.wifi = wifi or {'saved': True, 'ssid': 'home', 'connected': True, 'rssi': -50}

    def info(self):
        info = {'ok': True, 'cmd': 'INFO', 'fw': '1.0.0', 'cfg_v': 2, 'wifi': self.wifi}
        if self.firmware_keys:
            info['keys'] = {'link': mb_serial.fingerprint('link', self.link) if self.link else False,
                            'update': mb_serial.fingerprint('update', self.update) if self.update else False}
        return info

    def write_keys(self, body):
        self.writes += 1
        if not self.takes_keys:
            return False
        self.link, self.update = body['link_key'], body['update_key']
        return True

    def close(self):
        pass


class Home(unittest.TestCase):
    """A temporary home folder; the key and cache paths point into it."""

    def setUp(self):
        self.home = self.enterContext(tempfile.TemporaryDirectory())
        config = os.path.join(self.home, '.config', 'monitor-buddy')
        cache = os.path.join(self.home, '.cache', 'monitor-buddy')
        self.link_file, self.signing_file = os.path.join(config, 'link-key'), os.path.join(config, 'update-key.pem')
        for module, name, value in ((buddy_link, 'LINK_KEY_FILE', self.link_file), (buddy_flash, 'SIGNING_KEY', self.signing_file),
                                    (buddy_link, 'CONFIG_H', os.path.join(self.home, 'no-config.h')),
                                    (buddy_link, 'ADDRESS_FILE', os.path.join(cache, 'address'))):
            self.enterContext(mock.patch.object(module, name, value))

    def read(self, path):
        with open(path, 'rb') as file:
            return file.read()


class Keys(Home):
    def test_makes_both_keys_private(self):
        self.assertEqual(buddy_keys.ensure(), {'link': 'made', 'update': 'made'})
        for path in (self.link_file, self.signing_file):
            self.assertEqual(stat.S_IMODE(os.stat(path).st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(os.stat(os.path.dirname(self.link_file)).st_mode), 0o700)
        self.assertRegex(self.read(self.link_file).decode().strip(), r'^[0-9a-f]{64}$')
        self.assertEqual(buddy_keys.have(), {'link': True, 'update': True})

    def test_a_second_run_keeps_the_keys(self):
        buddy_keys.ensure()
        before = self.read(self.link_file), self.read(self.signing_file)
        self.assertEqual(buddy_keys.ensure(), {'link': 'kept', 'update': 'kept'})
        self.assertEqual((self.read(self.link_file), self.read(self.signing_file)), before)

    def test_new_key_refuses_to_replace_one(self):
        buddy_keys.ensure()
        with self.assertRaises(FileExistsError):
            buddy_flash.new_key()

    def test_loose_permissions_are_tightened(self):
        buddy_keys.ensure()
        os.chmod(self.link_file, 0o644)
        os.chmod(os.path.dirname(self.link_file), 0o755)
        buddy_keys.ensure()
        self.assertEqual(stat.S_IMODE(os.stat(self.link_file).st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(os.stat(os.path.dirname(self.link_file)).st_mode), 0o700)

    def test_a_broken_link_key_file_is_not_replaced(self):
        os.makedirs(os.path.dirname(self.link_file))
        with open(self.link_file, 'w') as file:
            file.write('not a key\n')
        with self.assertRaises(ValueError):
            buddy_keys.ensure()
        self.assertEqual(self.read(self.link_file), b'not a key\n')

    def test_the_key_of_an_own_build_is_taken_over(self):
        own = 'ab' * 32
        with open(buddy_link.CONFIG_H, 'w') as file:
            file.write(f'#define LINK_KEY "{own}"\n')
        self.assertEqual(buddy_keys.ensure()['link'], 'adopted')
        self.assertEqual(self.read(self.link_file).decode().strip(), own)

    def test_the_board_gets_the_public_half_only(self):
        buddy_keys.ensure()
        body = buddy_keys.for_board()
        self.assertEqual(set(body), {'link_key', 'update_key'})
        self.assertTrue(body['update_key'].startswith('-----BEGIN PUBLIC KEY-----\n'))
        self.assertNotIn('PRIVATE', body['update_key'])
        self.assertLess(len(json.dumps({'id': 2147483647, **body})) + len('@MB KEYS '), 2048)   # fits one @MB line

    def test_fingerprints_do_not_contain_the_key(self):
        buddy_keys.ensure()
        key, prints = buddy_keys.for_board()['link_key'], buddy_keys.fingerprints()
        self.assertRegex(prints['link'], r'^[0-9a-f]{8}$')
        self.assertNotIn(prints['link'], key)
        self.assertEqual(prints['link'], mb_serial.fingerprint('link', key))

    def test_the_link_uses_the_key_file(self):
        self.assertIsNone(buddy_link.link_cipher())
        buddy_keys.ensure()
        self.assertEqual(buddy_link.link_key(), buddy_keys.for_board()['link_key'])
        self.assertIsNotNone(buddy_link.link_cipher())

    def test_rotate_can_be_undone_and_forgotten(self):
        buddy_keys.ensure()
        old = self.read(self.link_file), self.read(self.signing_file)
        undo, forget = buddy_keys.rotate()
        self.assertNotEqual(self.read(self.link_file), old[0])
        self.assertNotEqual(self.read(self.signing_file), old[1])
        undo()
        self.assertEqual((self.read(self.link_file), self.read(self.signing_file)), old)
        self.assertEqual(sorted(os.listdir(os.path.dirname(self.link_file))), ['link-key', 'update-key.pem'])
        undo, forget = buddy_keys.rotate()
        forget()
        self.assertEqual(sorted(os.listdir(os.path.dirname(self.link_file))), ['link-key', 'update-key.pem'])
        self.assertNotEqual(self.read(self.link_file), old[0])

    def test_the_signed_update_matches_the_key_the_board_gets(self):
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        buddy_keys.ensure()
        with open(self.signing_file, 'rb') as file:
            signature = serialization.load_pem_private_key(file.read(), None).sign(b'firmware', ec.ECDSA(hashes.SHA256()))
        public = serialization.load_pem_public_key(buddy_keys.for_board()['update_key'].encode())
        public.verify(signature, b'firmware', ec.ECDSA(hashes.SHA256()))   # raises when it does not match


class Flash(unittest.TestCase):
    def test_only_an_app_image_goes_over_wifi(self):
        app = bytes([0xE9]) + bytes(0x9000)
        factory = bytearray(app)
        factory[0x8000:0x8002] = b'\xaa\x50'
        self.assertTrue(buddy_flash.is_app_image(app))
        self.assertFalse(buddy_flash.is_app_image(bytes(factory)))
        self.assertFalse(buddy_flash.is_app_image(b'hello'))


class SerialKeys(unittest.TestCase):
    def test_a_lost_reply_is_checked_by_fingerprint(self):
        body = {'link_key': 'cd' * 32, 'update_key': 'PEM'}
        held = {'link': mb_serial.fingerprint('link', body['link_key']), 'update': mb_serial.fingerprint('update', 'PEM')}
        with mock.patch.object(mb_serial, '_read_ok', return_value={'ok': True, 'keys': held}):
            self.assertTrue(mb_serial.verify_change(None, 'KEYS', body)[0])
        with mock.patch.object(mb_serial, '_read_ok', return_value={'ok': True, 'keys': {'link': False, 'update': held['update']}}):
            self.assertFalse(mb_serial.verify_change(None, 'KEYS', body)[0])

    def test_keys_are_sent_once(self):
        self.assertIn('KEYS', mb_serial.CHANGING)


class SetupRun(Home):
    """Whole runs of the setup command against the pretend Buddy."""

    def make(self, board=None, answers=(), **options):
        out = io.StringIO()
        setup = buddy_setup.Setup(home=self.home, repo=os.path.dirname(TOOLS), out=out, **options)
        setup.interactive = True
        setup.address_wait_s = 0
        self.commands, self.asked = [], []
        replies = list(answers)

        def run(*cmd, timeout=30):
            self.commands.append(cmd)
            if cmd[:2] == ('systemctl', '--user'):
                return 0, 'active' if cmd[2] == 'is-active' and any(c[2] in ('start', 'restart') for c in self.commands if c[:2] == ('systemctl', '--user')) else ''
            return 127, ''

        def question(text):
            self.asked.append(text)
            return replies.pop(0) if replies else 'n'

        setup.run, setup.question = run, question
        setup.which = lambda name: '/usr/bin/' + name if name in ('systemctl', 'loginctl', 'fish') else None
        setup.find_port = lambda: '/dev/ttyFAKE' if board else None
        setup.open_board = lambda port: board
        setup.ping = lambda: bool(board and board.link)
        setup.groups = lambda: ({'dialout'}, True)
        setup.firewall_blocks = lambda: False
        setup.network = lambda: ('wlan0', '192.168.1.0/24')
        return setup, out

    def go(self, *args, **options):
        setup, out = self.make(*args, **options)
        code = setup.main()
        return setup, out.getvalue(), code

    def assert_no_key_in(self, text):
        self.assertNotIn(self.read(self.link_file).decode().strip(), text)
        self.assertNotIn('PRIVATE KEY', text)
        self.assertNotIn(buddy_flash.public_key().split('\n')[1], text)

    def test_first_run_pairs_the_board(self):
        board = FakeBoard()
        setup, text, code = self.go(board)
        self.assertEqual(code, 0)
        self.assertEqual(board.writes, 1)
        self.assertEqual(board.link, buddy_keys.for_board()['link_key'])
        self.assertIn('the Buddy took the keys', text)
        self.assertIn('the Buddy answers over Wi-Fi', text)
        self.assert_no_key_in(text)
        unit = self.read(os.path.join(self.home, '.config', 'systemd', 'user', 'buddy-link.service')).decode()
        self.assertIn('tools/buddy_link.py', unit)
        self.assertTrue(os.path.islink(os.path.join(self.home, '.local', 'bin', 'buddy-say')))

    def test_second_run_skips_what_is_done(self):
        board = FakeBoard()
        self.go(board, answers=['y'])   # fish: yes
        keys = self.read(self.link_file), self.read(self.signing_file)
        state = self.read(os.path.join(self.home, '.config', 'monitor-buddy', 'setup.json'))
        setup, text, code = self.go(board)
        self.assertEqual(code, 0)
        self.assertEqual(board.writes, 1, 'the keys go to the board once')
        self.assertEqual((self.read(self.link_file), self.read(self.signing_file)), keys)
        self.assertEqual(self.asked, [], 'the optional questions are asked once')
        self.assertIn('already holds this computer\'s keys', text)
        self.assertIn('stays as it is', text)
        self.assertIn('the hook is already there', text)
        self.assertFalse(any(c[2] in ('daemon-reload', 'restart') for c in self.commands if c[0] == 'systemctl'), 'the service is left running')
        self.assertEqual(json.loads(state)['answers'], {'fish': True})
        self.assert_no_key_in(text)

    def test_resumes_after_a_run_without_the_board(self):
        setup, text, code = self.go(None)
        self.assertEqual(code, 0)
        self.assertIn('No Buddy found on USB', text)
        keys = self.read(self.link_file), self.read(self.signing_file)
        board = FakeBoard()
        setup, text, code = self.go(board)
        self.assertEqual(board.writes, 1)
        self.assertEqual((self.read(self.link_file), self.read(self.signing_file)), keys, 'the keys of the first run are the ones the board gets')
        self.assertNotIn('No Buddy found', text)

    def test_an_old_firmware_is_sent_to_the_installer(self):
        board = FakeBoard(firmware_keys=False)
        setup, text, code = self.go(board)
        self.assertEqual(board.writes, 0)
        self.assertIn('cannot take keys over USB', text)
        self.assertIn(buddy_setup.INSTALLER, text)

    def test_a_board_with_other_keys_is_left_alone_unless_confirmed(self):
        board = FakeBoard()
        board.link, board.update = 'ee' * 32, 'OTHER'
        setup, text, code = self.go(board, answers=['n'])
        self.assertEqual(board.writes, 0)
        self.assertIn('left alone', text)
        setup, text, code = self.go(board, answers=['y'])
        self.assertEqual(board.writes, 1)
        self.assertEqual(board.link, buddy_keys.for_board()['link_key'])

    def test_yes_never_overwrites_a_board_with_other_keys(self):
        board = FakeBoard()
        board.link, board.update = 'ee' * 32, 'OTHER'
        self.go(board, yes=True)
        self.assertEqual(board.writes, 0)
        self.assertEqual(self.asked, [])

    def test_dry_run_changes_nothing(self):
        board = FakeBoard()
        setup, text, code = self.go(board, dry=True)
        self.assertEqual(code, 0)
        self.assertEqual(board.writes, 0)
        self.assertEqual(os.listdir(self.home), [], 'no file is written')
        self.assertFalse(any(c[0] == 'systemctl' and c[2] not in ('show-environment', 'is-active') for c in self.commands))
        self.assertIn('Dry run: nothing was changed.', text)

    def test_rotation_needs_the_board(self):
        self.go(FakeBoard())
        keys = self.read(self.link_file), self.read(self.signing_file)
        setup, text, code = self.go(None, rotate=True)
        self.assertEqual(code, 1)
        self.assertIn('needs the Buddy on a USB cable', text)
        self.assertEqual((self.read(self.link_file), self.read(self.signing_file)), keys)

    def test_rotation_replaces_the_keys_on_both_sides(self):
        board = FakeBoard()
        self.go(board)
        old = self.read(self.link_file), self.read(self.signing_file)
        setup, text, code = self.go(board, rotate=True)
        self.assertEqual(code, 0)
        self.assertNotEqual(self.read(self.link_file), old[0])
        self.assertNotEqual(self.read(self.signing_file), old[1])
        self.assertEqual(board.link, buddy_keys.for_board()['link_key'])
        self.assertEqual(sorted(os.listdir(os.path.dirname(self.link_file))), ['link-key', 'setup.json', 'update-key.pem'], 'the old keys are gone')
        self.assertTrue(any(c[2] == 'restart' for c in self.commands if c[0] == 'systemctl'), 'the helper loads the new key')
        self.assert_no_key_in(text)

    def test_a_failed_rotation_puts_the_old_keys_back(self):
        board = FakeBoard()
        self.go(board)
        old = self.read(self.link_file), self.read(self.signing_file)
        board.takes_keys = False
        setup, text, code = self.go(board, rotate=True)
        self.assertEqual(code, 1)
        self.assertEqual((self.read(self.link_file), self.read(self.signing_file)), old)
        self.assertIn('old keys are back in place', text)

    def test_missing_desktop_parts_are_skipped_with_a_note(self):
        setup, text, code = self.go(FakeBoard())
        self.assertEqual(code, 0)
        self.assertIn('skipped: Microphone (needs PipeWire', text)
        self.assertIn('skipped: Focus timer (needs the GNOME desktop', text)
        self.assertIn('Screen lock', '\n'.join(setup.works))

    def test_no_systemd_is_a_note_not_a_failure(self):
        setup, out = self.make(FakeBoard())
        setup.which = lambda name: None
        self.assertEqual(setup.main(), 0)
        self.assertIn('no systemd user services', out.getvalue())
        self.assertIn('Start it yourself', out.getvalue())

    def test_a_board_without_wifi_is_explained(self):
        board = FakeBoard(wifi={'saved': False, 'ssid': '', 'connected': False})
        setup, text, code = self.go(board)
        self.assertIn('not on a Wi-Fi network yet', text)
        self.assertEqual(board.writes, 1)

    def test_claude_hooks_merge_into_existing_settings(self):
        folder = os.path.join(self.home, '.claude')
        os.makedirs(folder)
        own = {'model': 'x', 'hooks': {'Stop': [{'hooks': [{'type': 'command', 'command': 'echo mine'}]}]}}
        with open(os.path.join(folder, 'settings.json'), 'w') as file:
            json.dump(own, file)
        self.go(FakeBoard(), answers=['y', 'n'])   # claude yes, fish no
        settings = json.loads(self.read(os.path.join(folder, 'settings.json')))
        self.assertEqual(settings['model'], 'x')
        self.assertEqual(settings['hooks']['Stop'][0], own['hooks']['Stop'][0], 'his own hook stays first')
        self.assertEqual(len(settings['hooks']['Stop']), 2)
        self.assertEqual(sorted(settings['hooks']), ['Notification', 'PostToolUse', 'PreToolUse', 'SessionEnd', 'Stop', 'UserPromptSubmit'])
        self.assertEqual(json.loads(self.read(os.path.join(folder, 'settings.json.bak'))), own)
        self.go(FakeBoard())
        self.assertEqual(json.loads(self.read(os.path.join(folder, 'settings.json'))), settings, 'a second run adds nothing')

    def test_unreadable_claude_settings_are_left_alone(self):
        folder = os.path.join(self.home, '.claude')
        os.makedirs(folder)
        with open(os.path.join(folder, 'settings.json'), 'w') as file:
            file.write('{ not json')
        setup, text, code = self.go(FakeBoard(), answers=['y', 'n'])
        self.assertEqual(self.read(os.path.join(folder, 'settings.json')), b'{ not json')
        self.assertIn('could not be read', text)

    def test_undo_removes_what_setup_wrote_and_keeps_the_rest(self):
        folder = os.path.join(self.home, '.claude')
        os.makedirs(folder)
        own = {'hooks': {'Stop': [{'hooks': [{'type': 'command', 'command': 'echo mine'}]}]}}
        with open(os.path.join(folder, 'settings.json'), 'w') as file:
            json.dump(own, file)
        os.makedirs(os.path.join(self.home, '.config', 'google-chrome'))
        self.go(FakeBoard(), answers=['y', 'y', 'y'])
        wrote = [os.path.join(self.home, *parts) for parts in (
            ('.config', 'systemd', 'user', 'buddy-link.service'), ('.local', 'bin', 'buddy-say'),
            ('.local', 'share', 'applications', 'monitor-buddy.desktop'), ('.config', 'fish', 'conf.d', 'buddy.fish'),
            ('.config', 'google-chrome', 'NativeMessagingHosts', 'com.monitor_buddy.odoo.json'))]
        for path in wrote:
            self.assertTrue(os.path.lexists(path), path)
        keys = self.read(self.link_file)
        setup, out = self.make(None)
        self.assertEqual(setup.undo(), 0)
        for path in wrote:
            self.assertFalse(os.path.lexists(path), path)
        self.assertEqual(json.loads(self.read(os.path.join(folder, 'settings.json'))), own, 'his own hook stays, ours are gone')
        self.assertEqual(self.read(self.link_file), keys, 'the keys stay')
        self.assertTrue(any(c[2:5] == ('disable', '--now', 'buddy-link.service') for c in self.commands if c[0] == 'systemctl'))
        setup, out = self.make(None)
        setup.forget_keys = True
        setup.undo()
        self.assertFalse(os.path.exists(os.path.dirname(self.link_file)))

    def test_undo_leaves_a_fish_hook_of_his_own(self):
        path = os.path.join(self.home, '.config', 'fish', 'conf.d', 'buddy.fish')
        os.makedirs(os.path.dirname(path))
        with open(path, 'w') as file:
            file.write('# mine\n')
        setup, out = self.make(None)
        setup.undo()
        self.assertEqual(self.read(path), b'# mine\n')

    def test_from_the_package_nothing_is_added_to_the_home_menu(self):
        setup, out = self.make(FakeBoard())
        setup.packaged = True
        self.assertEqual(setup.main(), 0)
        self.assertFalse(os.path.exists(os.path.join(self.home, '.local', 'share', 'applications', 'monitor-buddy.desktop')))
        self.assertFalse(os.path.lexists(os.path.join(self.home, '.local', 'bin', 'buddy-say')))
        self.assertIn('came with the package', out.getvalue())

    def test_odoo_writes_the_host_manifest_and_lists_the_manual_step(self):
        os.makedirs(os.path.join(self.home, '.config', 'google-chrome'))
        setup, text, code = self.go(FakeBoard(), answers=['y', 'n'])
        manifest = json.loads(self.read(os.path.join(self.home, '.config', 'google-chrome', 'NativeMessagingHosts', 'com.monitor_buddy.odoo.json')))
        self.assertTrue(manifest['path'].endswith('tools/buddy_odoo_host.py'))
        self.assertEqual(manifest['allowed_origins'], ['chrome-extension://hpfaofllppcbcopbdbanihdnfnbkcgad/'])
        self.assertIn('Load unpacked', text)


class Firewall(SetupRun):
    """The firewall step: the Buddy's hello may come in from its network."""
    RULE = ('ufw', 'allow', 'in', 'on', 'wlan0', 'from', '192.168.1.0/24', 'to', 'any', 'port', '4747', 'proto', 'udp')

    def go_firewall(self, answers, sudo_works=True):
        setup, out = self.make(FakeBoard(), answers=answers)
        setup.firewall_blocks = lambda: True
        which, run = setup.which, setup.run
        setup.which = lambda name: '/usr/bin/sudo' if name == 'sudo' else which(name)
        setup.run = lambda *cmd, timeout=30: (self.commands.append(cmd), (0 if sudo_works else 1, ''))[1] if cmd[0] == 'sudo' else run(*cmd, timeout=timeout)
        return setup, setup.main(), out.getvalue()

    def sudo(self):
        return [c[1:] for c in self.commands if c[0] == 'sudo']

    def test_opens_the_port_to_the_buddys_network_once(self):
        _, code, text = self.go_firewall(['n', 'y'])   # fish: no, firewall: yes
        self.assertEqual(code, 0)
        self.assertEqual(self.sudo(), [self.RULE + ('comment', 'Monitor Buddy hello')])
        self.assertIn("the firewall lets the Buddy's hello in from 192.168.1.0/24", text)
        _, _, text = self.go_firewall([])
        self.assertEqual(self.sudo(), [], 'a second run asks for no password')
        self.assertIn("lets the Buddy's hello in", text)

    def test_left_closed_when_declined(self):
        _, _, text = self.go_firewall(['n', 'n'])
        self.assertEqual(self.sudo(), [])
        self.assertNotIn("lets the Buddy's hello in", text)

    def test_a_failed_sudo_leaves_it_to_do(self):
        setup, _, text = self.go_firewall(['n', 'y'], sudo_works=False)
        self.assertIn("The firewall still drops the Buddy's hello.", text)
        self.assertNotIn('firewall', setup.state['done'])

    def test_undo_takes_the_rule_out(self):
        setup, _, _ = self.go_firewall(['n', 'y'])
        self.commands.clear()
        setup.undo()
        self.assertIn(('ufw', 'delete') + self.RULE[1:], self.sudo())
        self.assertNotIn('firewall', setup.state['done'])


class ClaudeHook(unittest.TestCase):
    def setUp(self):
        self.folder = self.enterContext(tempfile.TemporaryDirectory())
        self.shared, self.sessions = os.path.join(self.folder, 'claude'), os.path.join(self.folder, 'sessions')
        self.enterContext(mock.patch.object(buddy_claude_hook, 'CLAUDE_FILE', self.shared))
        self.enterContext(mock.patch.object(buddy_claude_hook, 'SESSIONS_DIR', self.sessions))

    def text(self, path):
        try:
            with open(path) as file:
                return file.read().strip()
        except OSError:
            return None

    def test_the_paths_are_the_helpers(self):
        self.assertEqual(os.path.expanduser('~/.cache/monitor-buddy/claude'), buddy_link.CLAUDE_FILE)
        self.assertEqual(os.path.expanduser('~/.cache/monitor-buddy/sessions'), buddy_link.SESSIONS_DIR)

    def test_a_session_from_question_to_end(self):
        own = os.path.join(self.sessions, 'abc-1')
        data = {'session_id': 'abc-1/../x'[:5]}
        buddy_claude_hook.note('prompt', data, 'proj')
        self.assertEqual(self.text(own), 'working proj')
        buddy_claude_hook.note('needs', data, 'proj')
        self.assertEqual((self.text(self.shared), self.text(own)), ('needs proj', 'needs proj'))
        buddy_claude_hook.note('tool', data, 'proj')
        self.assertEqual((self.text(self.shared), self.text(own)), (None, 'working proj'))
        buddy_claude_hook.note('done', data, 'proj')
        self.assertEqual((self.text(self.shared), self.text(own)), ('done proj', 'done proj'))
        buddy_claude_hook.note('tool', data, 'proj')
        self.assertEqual(self.text(self.shared), 'done proj', 'a tool call clears only a question')
        buddy_claude_hook.note('end', data, 'proj')
        self.assertIsNone(self.text(own))

    def test_the_session_id_cannot_leave_the_folder(self):
        buddy_claude_hook.note('prompt', {'session_id': '../../evil'}, 'proj')
        self.assertEqual(os.listdir(self.sessions), ['evil'])

    def test_it_never_fails_or_prints(self):
        done = subprocess.run([sys.executable, os.path.join(TOOLS, 'buddy_claude_hook.py'), 'tool'], input='not json', capture_output=True, text=True,
                              env={**os.environ, 'HOME': self.folder})
        self.assertEqual((done.returncode, done.stdout, done.stderr), (0, '', ''))


if __name__ == '__main__':
    unittest.main()
