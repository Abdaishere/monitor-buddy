#!/usr/bin/env python3
"""Set up this computer for a Monitor Buddy. One command, safe to run again:

    ./setup.sh                  (or: python3 tools/buddy_setup.py)

    ./setup.sh --dry-run        only say what it would do; changes nothing
    ./setup.sh --status         only show what works and what is left
    ./setup.sh --ask-again      ask the optional questions (Odoo, Claude, fish) again
    ./setup.sh --yes            no questions: optional parts keep their last answer, else stay off
    ./setup.sh --rotate-keys    replace both keys (the Buddy must be on USB)
    ./setup.sh --port PORT      the Buddy's USB port, when it is not found by itself
    ./setup.sh --undo           take the helper, commands and hooks off this computer again

What it does, in order. Each step first looks at what is there and skips what
is already done, so a second run only does what is left:

  1. checks that Python has what the tools need;
  2. makes the two keys on this computer (tools/buddy_keys.py). Keys that are
     there are never replaced, and no key is ever printed;
  3. gives the keys to the Buddy over USB ("@MB KEYS", tools/mb_serial.py);
  4. starts the helper (tools/buddy_link.py) as a background service;
  5. puts the buddy-say command on your PATH;
  6. looks which desktop features this computer can feed (screen lock,
     microphone, pull requests, calendar) and says so, instead of failing;
  7. asks once about the optional parts: Odoo (Chrome), Claude Code, fish;
  8. checks the Wi-Fi link and, asked once, lets the Buddy's hello through the
     firewall (ufw) from its network, so a new address from the router finds
     the helper without USB; then prints what works and what is left.

The answers to the questions are kept in ~/.config/monitor-buddy/setup.json.
The firmware itself is installed from the browser: see "Set up" in README.md.
"""
import argparse
import glob
import ipaddress

try:
    import grp
except ImportError:   # not Linux: the app (tools/buddy_app.py) still uses Board from here
    grp = None
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

REPO = os.path.realpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
PORT_GLOB = '/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*'
UNIT = 'buddy-link.service'
ODOO_HOST = 'com.monitor_buddy.odoo'
ODOO_EXTENSION = 'hpfaofllppcbcopbdbanihdnfnbkcgad'   # pinned by the key in tools/chrome-odoo/manifest.json
INSTALLER = 'https://abdaishere.github.io/monitor-buddy/'
HELLO_RULE = 'Monitor Buddy hello'   # the comment on the firewall rule
# Claude Code hooks: (event, matcher or None for all, what buddy_claude_hook.py is told)
CLAUDE_HOOKS = (
    ('Notification', 'permission_prompt|elicitation_dialog', 'needs'),
    ('PreToolUse', 'AskUserQuestion', 'needs'),
    ('Stop', None, 'done'),
    ('UserPromptSubmit', None, 'prompt'),
    ('PostToolUse', None, 'tool'),
    ('SessionEnd', None, 'end'),
)
FISH_HOOK = '''# Monitor Buddy: shows on the Buddy when a command that ran over a minute ends.
# Written by tools/buddy_setup.py; delete this file to turn it off.
function __buddy_done --on-event fish_postexec
    set -l code $status
    test "$CMD_DURATION" -gt 60000; or return
    command -q buddy-say; or return
    set -l words (string split -n ' ' -- $argv[1])
    contains -- "$words[1]" vim nvim vi nano ssh claude man less more htop top btop watch tmux fish bash zsh python python3 ipython psql; and return
    if test $code -eq 0
        buddy-say "DONE: $words[1..2]" >/dev/null 2>&1 &
    else
        buddy-say "FAILED $code: $words[1..2]" >/dev/null 2>&1 &
    end
    disown 2>/dev/null
end
'''


class Stop(Exception):
    """Setup cannot go on; the message says what to do."""


class Board:
    """The Buddy on a USB port, through tools/mb_serial.py."""

    def __init__(self, port):
        import mb_serial
        self.serial, self.port = mb_serial, port
        self.s = mb_serial.open_port(port)

    def info(self):
        return self.serial.command(self.s, 'INFO')

    def write_keys(self, body):
        """Sends the keys once and waits for the restart. True when the Buddy took them."""
        change = self.serial.change(self.s, self.port, 'KEYS', body, wait_restart=True)
        self.s = change.s
        return change.ok

    def get_config(self):
        """The Buddy's settings (GET), or None when it does not answer."""
        answer = self.serial.command(self.s, 'GET')
        return answer.get('config') if answer.get('ok') else None

    def set_config(self, patch, wifi=None):
        """Changes settings and, with wifi {'ssid', 'password'}, the network (SET, sent once); the Buddy
        restarts. (ok, the Buddy's errors by key)."""
        body = {'config': patch} if patch else {}
        if wifi:
            body['wifi'] = wifi
        change = self.serial.change(self.s, self.port, 'SET', body, wait_restart=True)
        self.s = change.s
        return change.ok, (change.reply or {}).get('errors') or {}

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


class Setup:
    """One run. Everything that touches the system goes through a method of
    this class, so the tests replace them (tools/test_buddy_setup.py)."""

    def __init__(self, home=None, repo=REPO, dry=False, yes=False, ask_again=False, rotate=False, port=None, out=None):
        self.home = home or os.path.expanduser('~')
        self.repo, self.dry, self.yes, self.ask_again, self.rotate, self.port = repo, dry, yes, ask_again, rotate, port
        self.out = out or sys.stdout
        self.interactive = sys.stdin.isatty()   # without a terminal nothing is asked
        self.address_wait_s = 25
        self.forget_keys = False
        self.packaged = os.path.exists(os.path.join(self.repo, 'PACKAGED'))   # installed from the .deb: menu entry and commands are there
        self.repair_ok = False   # the app's tick box: a Buddy that holds other keys may be paired with this computer
        self.state_file = os.path.join(self.home, '.config', 'monitor-buddy', 'setup.json')
        self.state = self._load_state()
        self.works, self.left = [], []
        self.service_was_stopped = False
        self.keys_written = False
        self.board_info = None

    # ── the outside world (replaced in the tests) ─────────────────────────
    def run(self, *cmd, timeout=30):
        """(exit code, output) of a command; (127, '') when it does not exist."""
        try:
            done = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        except (OSError, subprocess.TimeoutExpired):
            return 127, ''
        return done.returncode, done.stdout.strip()

    def which(self, name):
        return shutil.which(name)

    def find_port(self):
        if self.port:
            return self.port
        found = sorted(glob.glob(PORT_GLOB))
        return found[0] if found else None

    def open_board(self, port):
        return Board(port)

    def question(self, text):
        return input(text)

    def ping(self):
        """True when the Buddy answers on the Wi-Fi link."""
        import buddy_link
        cipher = buddy_link.link_cipher()
        try:
            with open(buddy_link.ADDRESS_FILE) as file:
                address = file.read().strip()
        except OSError:
            return False
        return bool(cipher and address) and any(buddy_link.send_wifi(cipher, address, b'P') is not None for _ in range(3))

    def groups(self):
        """(the groups this session has, whether /etc/group lists the user in dialout)."""
        mine = {grp.getgrgid(g).gr_name for g in os.getgroups()}
        try:
            import getpass
            listed = getpass.getuser() in grp.getgrnam('dialout').gr_mem
        except KeyError:
            listed = False
        return mine, listed

    def firewall_blocks(self):
        """True when ufw is on and drops what nobody asked for (read without root)."""
        def setting(path, name):
            try:
                with open(path) as file:
                    found = re.search(rf'^{name}\s*=\s*"?(\w+)', file.read(), re.M)
            except OSError:
                return ''
            return found[1] if found else ''
        return bool(self.which('ufw')) and setting('/etc/ufw/ufw.conf', 'ENABLED') == 'yes' \
            and setting('/etc/default/ufw', 'DEFAULT_INPUT_POLICY') in ('DROP', 'REJECT')

    def network(self):
        """(interface, network) this computer reaches the Buddy through, like
        ('wlan0', '192.168.1.0/24'), or None before its address is known."""
        import buddy_link
        try:
            with open(buddy_link.ADDRESS_FILE) as file:
                address = file.read().strip()
        except OSError:
            return None
        code, route = self.run('ip', '-4', '-o', 'route', 'get', address)
        found = re.search(r'\bdev (\S+) .*\bsrc (\S+)', route)
        if code or not found:
            return None
        code, addresses = self.run('ip', '-4', '-o', 'addr', 'show', 'dev', found[1])
        mine = re.search(rf'inet ({re.escape(found[2])}/\d+)', addresses)
        return (found[1], str(ipaddress.ip_interface(mine[1]).network)) if not code and mine else None

    # ── small helpers ─────────────────────────────────────────────────────
    def say(self, text=''):
        print(text, file=self.out, flush=True)

    def step(self, title):
        self.say(f'\n{title}')

    def ok(self, text):
        self.say(f'  ✓ {text}')

    def info(self, text):
        self.say(f'  • {text}')

    def todo(self, text, how):
        self.say(f'  ! {text}')
        self.left.append((text, how))

    def would(self, text):
        self.say(f'  → would {text}')

    def _load_state(self):
        try:
            with open(self.state_file) as file:
                state = json.load(file)
        except (OSError, ValueError):
            state = {}
        if not isinstance(state, dict):
            state = {}
        state.setdefault('answers', {})
        state.setdefault('done', {})
        return state

    def _save_state(self):
        if self.dry:
            return
        os.makedirs(os.path.dirname(self.state_file), mode=0o700, exist_ok=True)
        part = self.state_file + '.part'
        with open(part, 'w') as file:
            json.dump(self.state, file, indent=1)
        os.replace(part, self.state_file)

    def mark(self, name):
        """Record that a step is done, so the next run can say so."""
        if name not in self.state['done']:
            self.state['done'][name] = time.strftime('%Y-%m-%d %H:%M')
            self._save_state()

    def wants(self, name, text):
        """The answer to an optional part: asked once, then remembered."""
        answers = self.state['answers']
        if name in answers and not self.ask_again:
            return answers[name]
        if self.yes or self.dry or not self.interactive:
            if name not in answers:
                self.info(f'would ask: {text}' if self.dry else f'not asked (no terminal, or --yes): {text}')
            return answers.get(name, False)
        answer = self.question(f'  ? {text} [y/N] ').strip().lower() in ('y', 'yes')
        answers[name] = answer
        self._save_state()
        return answer

    def write_file(self, path, text, mode=0o644):
        """Write a file of our own; an existing one with other content is kept beside it as .bak."""
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if os.path.exists(path):
            shutil.copy2(path, path + '.bak')
        with os.fdopen(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, mode), 'w') as file:
            file.write(text)

    # ── 1. Python ─────────────────────────────────────────────────────────
    def check_python(self):
        self.step('1. What the tools need')
        missing = []
        for module, package in (('cryptography', 'python3-cryptography'), ('serial', 'python3-serial'), ('dateutil', 'python3-dateutil')):
            try:
                __import__(module)
            except ImportError:
                missing.append(package)
        if missing:
            raise Stop('Python is missing a few parts. Install them, then run ./setup.sh again:\n'
                       f'      sudo apt install {" ".join(missing)}\n'
                       '  (other systems: pip install cryptography pyserial python-dateutil)')
        self.ok('Python has everything')

    # ── 2. keys ───────────────────────────────────────────────────────────
    def make_keys(self):
        import buddy_keys
        self.step('2. Keys (they stay on this computer)')
        there = buddy_keys.have()
        if self.dry:
            for name, has in there.items():
                self.ok(f'the {name} key is already there; it stays as it is') if has else self.would(f'make the {name} key')
            return
        if self.rotate:
            self.info('--rotate-keys: new keys are made in step 3, once the Buddy is there to take them')
            if all(there.values()):
                return
        done = buddy_keys.ensure()
        words = {'kept': 'is already there; it stays as it is', 'made': 'was made', 'adopted': 'was taken over from your own build (config/config.h)'}
        for name in ('link', 'update'):
            self.ok(f'the {name} key {words[done[name]]}')
        self.mark('keys')

    # ── 3. the Buddy on USB ───────────────────────────────────────────────
    def pair_board(self):
        import buddy_keys
        self.step('3. The Buddy (USB cable)')
        port = self.find_port()
        if not port:
            if self.rotate:
                raise Stop('--rotate-keys needs the Buddy on a USB cable, so it can take the new keys. Nothing was changed.')
            self.todo('No Buddy found on USB, so it has not been given the keys.',
                      'Plug the Buddy into this computer with a USB cable (one that carries data) and run the setup again.')
            return
        if self.dry:
            self.would(f'look at the Buddy on {port} and give it the keys if it has none (dry run: the port is not opened)')
            return
        code, active = self.run('systemctl', '--user', 'is-active', UNIT)
        if active == 'active':   # the helper holds the port
            self.run('systemctl', '--user', 'stop', UNIT)
            self.service_was_stopped = True
        try:
            board = self.open_board(port)
        except OSError as error:
            if self.rotate:
                raise Stop(f'--rotate-keys could not open the Buddy\'s port ({error}). Nothing was changed.')
            _, listed = self.groups()
            if isinstance(error, PermissionError) or 'ermission' in str(error):
                self.todo('This account may not use USB serial ports yet.',
                          'Log out and in again.' if listed else 'Run: sudo usermod -aG dialout $USER   then log out and in again, and run the setup.')
            else:
                self.todo(f'The Buddy\'s port did not open ({error}).', 'Close other programs that use it (a serial monitor, the web installer tab) and run the setup again.')
            return
        try:
            self._pair(board, buddy_keys)
        finally:
            board.close()

    def _pair(self, board, buddy_keys):
        try:
            info = board.info()
        except (TimeoutError, OSError):
            if self.rotate:
                raise Stop('--rotate-keys: the Buddy did not answer on USB. Nothing was changed.')
            self.todo('Something is on the USB port, but it does not answer like a Monitor Buddy.',
                      f'Install the firmware first: open {INSTALLER} in Chrome or Edge and click Install. Then run the setup again.')
            return
        self.board_info = info
        if 'keys' not in info:
            if self.rotate:
                raise Stop('--rotate-keys: this Buddy runs a firmware that cannot take keys over USB. Nothing was changed.')
            self.todo(f'This Buddy runs a firmware ({info.get("fw", "unknown")}) that cannot take keys over USB.',
                      f'Update it first: open {INSTALLER} in Chrome or Edge and click Install. Your settings and Wi-Fi are kept. Then run the setup again.')
            return
        held = info['keys']
        if self.rotate:
            undo, forget = buddy_keys.rotate()
            if board.write_keys(buddy_keys.for_board()) and self._holds(board, buddy_keys):
                forget()
                self.keys_written = True
                self.ok('new keys made and given to the Buddy; the old ones are deleted')
                self.works.append('The Buddy holds this computer\'s (new) keys')
            else:
                undo()
                raise Stop('The Buddy did not take the new keys, so your old keys are back in place. Nothing changed.')
            return
        mine = buddy_keys.fingerprints()
        if held.get('link') == mine['link'] and held.get('update') == mine['update']:
            self.ok('the Buddy already holds this computer\'s keys')
            self.works.append('The Buddy holds this computer\'s keys')
            self.mark('board')
            return
        if (held.get('link') or held.get('update')) and not self._confirm_repair():
            self.todo('This Buddy holds the keys of another computer (or older ones). It was left alone.',
                      'To pair it with this computer instead, run the setup again and say yes.')
            return
        if board.write_keys(buddy_keys.for_board()) and self._holds(board, buddy_keys):
            self.keys_written = True
            self.ok('the Buddy took the keys (over USB) and restarted')
            self.works.append('The Buddy holds this computer\'s keys')
            self.mark('board')
        else:
            self.todo('The Buddy did not take the keys.', 'Unplug it, plug it in again and run the setup again.')

    def _confirm_repair(self):
        if self.repair_ok:
            return True
        if self.yes or not self.interactive:
            return False
        return self.question('  ? This Buddy holds other keys (another computer\'s, or older ones). Pair it with this computer instead? [y/N] ').strip().lower() in ('y', 'yes')

    def _holds(self, board, buddy_keys):
        try:
            self.board_info = board.info()
        except (TimeoutError, OSError):
            return False
        held, mine = self.board_info.get('keys') or {}, buddy_keys.fingerprints()
        return held.get('link') == mine['link'] and held.get('update') == mine['update']

    # ── 4. the helper as a service ────────────────────────────────────────
    def unit_text(self):
        command = f'{sys.executable} {os.path.join(self.repo, "tools", "buddy_link.py")}'
        mine, listed = self.groups()
        if 'dialout' not in mine and listed:   # in the group, but this login is older than that
            command = f'/usr/bin/sg dialout -c "{command}"'
        return ('[Unit]\nDescription=Monitor Buddy helper: tells the Buddy what this computer is doing\n\n'
                f'[Service]\nExecStart={command}\nRestart=always\nRestartSec=10\n\n[Install]\nWantedBy=default.target\n')

    def install_service(self):
        self.step('4. The helper (runs in the background)')
        if not self.which('systemctl') or self.run('systemctl', '--user', 'show-environment')[0] != 0:
            self.todo('This system has no systemd user services, so the helper is not started for you.',
                      f'Start it yourself when you log in: python3 {os.path.join(self.repo, "tools", "buddy_link.py")}')
            return
        path = os.path.join(self.home, '.config', 'systemd', 'user', UNIT)
        text = self.unit_text()
        try:
            with open(path) as file:
                same = file.read() == text
        except OSError:
            same = False
        if self.dry:
            self.ok('the service is already set up') if same else self.would(f'write {path} and start the service')
            return
        if not same:
            self.write_file(path, text)
            self.run('systemctl', '--user', 'daemon-reload')
        self.run('systemctl', '--user', 'enable', UNIT)
        if not same or self.service_was_stopped or self.keys_written:
            self.run('systemctl', '--user', 'restart', UNIT)
        else:
            self.run('systemctl', '--user', 'start', UNIT)
        self.service_was_stopped = False
        if self.run('systemctl', '--user', 'is-active', UNIT)[1] == 'active':
            self.ok('the helper is running' + (' (it was already set up)' if same else ' and starts by itself when you log in'))
            self.works.append('The helper runs in the background (logs: journalctl --user -u buddy-link)')
            self.mark('service')
        else:
            self.todo('The helper did not start.', 'Look at why: journalctl --user -u buddy-link -n 20')
        mine, listed = self.groups()
        if 'dialout' not in mine and not listed:
            self.todo('This account may not use USB serial ports, so the helper cannot talk to the Buddy over the cable.',
                      'Run: sudo usermod -aG dialout $USER   then log out and in again.')

    # ── 5. buddy-say ──────────────────────────────────────────────────────
    def install_say(self):
        self.step('5. The buddy-say command')
        link, target = os.path.join(self.home, '.local', 'bin', 'buddy-say'), os.path.join(self.repo, 'tools', 'buddy_say.py')
        if self.packaged:
            self.ok('buddy-say came with the package')
            self.works.append('buddy-say TEXT shows a line on the Buddy')
            return
        if os.path.islink(link) and os.path.realpath(link) == os.path.realpath(target):
            self.ok('buddy-say is already there')
        elif os.path.lexists(link):
            self.info(f'{link} is something else; it was left alone')
            return
        elif self.dry:
            self.would(f'link {link} to tools/buddy_say.py')
            return
        else:
            os.makedirs(os.path.dirname(link), exist_ok=True)
            os.symlink(target, link)
            self.ok('buddy-say installed: try   buddy-say hello')
        self.works.append('buddy-say TEXT shows a line on the Buddy')
        if os.path.dirname(link) not in os.environ.get('PATH', '').split(os.pathsep):
            self.info('~/.local/bin is not on your PATH yet; on most systems it is after the next login')

    def install_app(self):
        """The app-menu entry of tools/buddy_app.py (setup and "say" without a terminal)."""
 
        if self.packaged:
            self.ok('Monitor Buddy is in your app menu (it came with the package)')
            self.works.append('Monitor Buddy in the app menu: type a line and it shows on the Buddy')
            return
        path = os.path.join(self.home, '.local', 'share', 'applications', 'monitor-buddy.desktop')
        app = f'{sys.executable} {os.path.join(self.repo, "tools", "buddy_app.py")}'
        text = ('[Desktop Entry]\nType=Application\nName=Monitor Buddy\nComment=Set up your Monitor Buddy and show a line on it\n'
                f'Exec={app}\nIcon={os.path.join(self.repo, "tools", "app", "icon.svg")}\nTerminal=false\nCategories=Utility;\n'
                'Actions=say;\n\n[Desktop Action say]\nName=Say something\n'
                f'Exec={app} --say\n')
        try:
            with open(path) as file:
                same = file.read() == text
        except OSError:
            same = False
        if same:
            self.ok('Monitor Buddy is in your app menu')
        elif self.dry:
            self.would(f'add Monitor Buddy to your app menu ({path})')
            return
        else:
            self.write_file(path, text)
            self.ok('Monitor Buddy added to your app menu: setup and "say" without a terminal')
        self.works.append('Monitor Buddy in the app menu: type a line and it shows on the Buddy')

    # ── 6. what this desktop can feed ─────────────────────────────────────
    def look_at_desktop(self):
        self.step('6. What this computer can tell the Buddy')
        desktop = os.environ.get('XDG_CURRENT_DESKTOP', '')
        gnome = 'GNOME' in desktop.upper()

        def feature(works, name, missing):
            if works:
                self.ok(name)
                self.works.append(name)
            else:
                self.info(f'skipped: {name.split(":")[0]} ({missing}). Everything else still works.')

        feature(self.which('loginctl'), 'Screen lock: the face knows when you are away, and the privacy lock works', 'needs systemd-logind')
        feature(self.which('pw-dump'), 'Microphone: ON AIR while you are in a call', 'needs PipeWire; this system has no pw-dump')
        feature(gnome and self.which('gsettings') and self.which('gdbus'), 'Focus timer: notifications go quiet while the Buddy\'s timer runs',
                f'needs the GNOME desktop; this one is {desktop or "unknown"}')
        feature(self.which('gh') and self.run('gh', 'auth', 'status')[0] == 0, 'Pull requests: your GitHub pull requests and their checks',
                'needs the GitHub CLI, signed in: install gh and run   gh auth login')
        feature(glob.glob(os.path.join(self.home, '.cache', 'evolution', 'calendar', '*', 'cache.db')), 'Calendar: meetings from GNOME Online Accounts',
                'needs a calendar account in GNOME Settings → Online Accounts')

    # ── 7. optional parts ─────────────────────────────────────────────────
    def optional_odoo(self):
        browsers = [d for d in ('google-chrome', 'chromium') if os.path.isdir(os.path.join(self.home, '.config', d))]
        if not browsers:
            self.info('Odoo page: skipped (needs Chrome or Chromium, which was not found)')
            return
        if not self.wants('odoo', 'Show your Odoo hours and tasks? (for accounts on www.odoo.com, through a small Chrome extension)'):
            self.info('Odoo page: off (./setup.sh --ask-again to change)')
            return
        host = os.path.join(self.repo, 'tools', 'buddy_odoo_host.py')
        text = json.dumps({'name': ODOO_HOST, 'description': 'Monitor Buddy: saves the Odoo summary from the Monitor Buddy extension',
                           'path': host, 'type': 'stdio', 'allowed_origins': [f'chrome-extension://{ODOO_EXTENSION}/']}, indent=2) + '\n'
        would = False
        for browser in browsers:
            path = os.path.join(self.home, '.config', browser, 'NativeMessagingHosts', ODOO_HOST + '.json')
            try:
                with open(path) as file:
                    same = file.read() == text
            except OSError:
                same = False
            if same:
                continue
            if self.dry:
                self.would(f'write {path}')
                would = True
            else:
                self.write_file(path, text)
                os.chmod(host, os.stat(host).st_mode | 0o111)
        if would:
            return
        fresh = os.path.join(self.home, '.cache', 'monitor-buddy', 'odoo.json')
        if os.path.exists(fresh) and time.time() - os.path.getmtime(fresh) < 1800:
            self.ok('Odoo: the extension is sending data')
            self.works.append('Odoo page: hours and tasks from the Chrome extension')
        else:
            self.ok('Odoo: this computer is ready for the extension')
            self.left.append(('Odoo: add the extension to Chrome (three clicks, once).',
                              f'In Chrome open chrome://extensions, switch on "Developer mode", click "Load unpacked" and pick the folder {os.path.join(self.repo, "tools", "chrome-odoo")}. Then sign in to www.odoo.com in that Chrome.'))

    def optional_claude(self):
        folder = os.path.join(self.home, '.claude')
        if not os.path.isdir(folder) and not self.which('claude'):
            self.info('Claude Code: skipped (not installed here)')
            return
        if not self.wants('claude', 'Show on the Buddy when a Claude Code session waits for you or has finished?'):
            self.info('Claude Code: off (./setup.sh --ask-again to change)')
            return
        path = os.path.realpath(os.path.join(folder, 'settings.json'))
        try:
            with open(path) as file:
                settings = json.load(file)
        except FileNotFoundError:
            settings = {}
        except (OSError, ValueError):
            self.todo('Claude Code: its settings.json could not be read, so it was left alone.', f'Check {path}, then run the setup again.')
            return
        if not isinstance(settings, dict) or not isinstance(settings.get('hooks', {}), dict):
            self.todo('Claude Code: settings.json has an unexpected shape, so it was left alone.', f'Check {path}, then run the setup again.')
            return
        hooks = settings.setdefault('hooks', {})
        if 'monitor-buddy/claude' in json.dumps(hooks):
            self.ok('Claude Code: your own Monitor Buddy hooks are already there; they were left alone')
            self.works.append('Claude Code: the face shows waiting and finished sessions')
            return
        script, added = os.path.join(self.repo, 'tools', 'buddy_claude_hook.py'), 0
        for event, matcher, word in CLAUDE_HOOKS:
            command = f'{sys.executable} {script} {word}'
            entries = hooks.setdefault(event, [])
            if not isinstance(entries, list) or command in json.dumps(entries):
                continue
            entry = {'hooks': [{'type': 'command', 'command': command}]}
            if matcher:
                entry['matcher'] = matcher
            entries.append(entry)
            added += 1
        if added and self.dry:
            self.would(f'add {added} hooks to {path} (the file as it is now would be kept as settings.json.bak)')
            return
        if added:
            self.write_file(path, json.dumps(settings, indent=2) + '\n', mode=0o600)
        self.ok('Claude Code: hooks ' + ('added (they apply to sessions you start from now on)' if added else 'are already there'))
        self.works.append('Claude Code: the face shows waiting and finished sessions')

    def optional_fish(self):
        if not self.which('fish'):
            self.info('fish shell: skipped (not installed here)')
            return
        if not self.wants('fish', 'Show on the Buddy when a terminal command that ran over a minute ends (fish shell)?'):
            self.info('fish shell: off (./setup.sh --ask-again to change)')
            return
        path = os.path.join(self.home, '.config', 'fish', 'conf.d', 'buddy.fish')
        if os.path.exists(path):
            with open(path) as file:
                own = file.read() != FISH_HOOK
            self.ok('fish shell: the hook is already there' + (' (your own version; it was left alone)' if own else ''))
        elif self.dry:
            self.would(f'write {path}')
            return
        else:
            self.write_file(path, FISH_HOOK)
            self.ok('fish shell: hook added (new terminals have it)')
        self.works.append('fish shell: long commands report DONE or FAILED on the Buddy')

    def optional_parts(self):
        self.step('7. Optional parts')
        self.optional_odoo()
        self.optional_claude()
        self.optional_fish()

    # ── 8. the link, and the summary ──────────────────────────────────────
    def check_link(self):
        self.step('8. The Wi-Fi link')
        import buddy_link
        if self.dry:
            self.would('ask the Buddy over Wi-Fi whether it hears this computer')
            return
        wifi = (self.board_info or {}).get('wifi') or {}
        if self.board_info is not None and not wifi.get('connected'):
            self.todo('The Buddy is not on a Wi-Fi network yet.' if not wifi.get('saved') else f'The Buddy is not connected to "{wifi.get("ssid", "")}" right now.',
                      f'Open {INSTALLER} in Chrome or Edge, connect, and fill in your Wi-Fi under "Change settings". Until then the helper talks to the Buddy over the USB cable.')
            return
        if not self.state['done'].get('board') and not self.keys_written and not os.path.exists(buddy_link.ADDRESS_FILE):
            self.info('not checked: the Buddy has not been set up from this computer yet')
            return
        end = time.monotonic() + (self.address_wait_s if self.board_info is not None else 0)   # the helper learns the address over USB
        while not os.path.exists(buddy_link.ADDRESS_FILE) and time.monotonic() < end:
            time.sleep(1)
        if self.ping():
            self.ok('the Buddy answers over Wi-Fi')
            self.works.append('The Wi-Fi link works: the Buddy needs no cable to this computer any more, only power')
            self.mark('link')
        elif not os.path.exists(buddy_link.ADDRESS_FILE):
            self.todo('This computer does not know the Buddy\'s address yet.',
                      'Leave the Buddy on the USB cable for a minute while it is on Wi-Fi: the helper picks the address up by itself. Then run the setup again to check.')
        else:
            self.todo('The Buddy did not answer over Wi-Fi.',
                      'Check that the Buddy and this computer are on the same network. If the Buddy changed network, plug it in by USB for a minute so the helper learns the new address.')

    def firewall_rule(self, interface, network):
        import buddy_link
        return ['ufw', 'allow', 'in', 'on', interface, 'from', network, 'to', 'any', 'port', str(buddy_link.LINK_PORT), 'proto', 'udp']

    def allow_hellos(self):
        """Lets the Buddy's hello (linkHello() in src/main.cpp) through ufw from the
        Buddy's network: when the router gives the Buddy a new address, the hello
        tells the helper, without the USB cable."""
        if not self.firewall_blocks():
            return
        net = self.network()
        if not net:
            self.info('firewall: looked at once this computer knows the Buddy\'s address')
            return
        interface, network = net
        if self.state['done'].get('firewall') == f'{interface} {network}':
            self.ok(f'the firewall lets the Buddy\'s hello in from {network}')
            self.works.append('A new address from the router: the Buddy tells this computer by itself')
            return
        asked = 'firewall' in self.state['answers']
        if not self.wants('firewall', f'Let the Buddy tell this computer when the router gives it a new address? '
                                      f'The firewall then lets UDP port 4747 in from {network}, and asks for your password.'):
            if asked:
                self.info('firewall: closed to the Buddy\'s hello. If the Buddy gets a new address, plug it in by USB for a minute')
            return
        rule = self.firewall_rule(interface, network) + ['comment', HELLO_RULE]
        if self.dry:
            self.would(f'let UDP 4747 in from {network} on {interface} (ufw; it asks for your password)')
            return
        elevate = 'sudo' if self.interactive else 'pkexec'   # the app has no terminal: pkexec shows a password window
        if not self.which(elevate) or self.run(elevate, *rule, timeout=300)[0] != 0:
            self.todo('The firewall still drops the Buddy\'s hello.', 'In a terminal:   sudo ' + shlex.join(rule))
            return
        self.state['done']['firewall'] = f'{interface} {network}'
        self._save_state()
        self.ok(f'the firewall lets the Buddy\'s hello in from {network}')
        self.works.append('A new address from the router: the Buddy tells this computer by itself')

    def undo(self):
        """Takes this computer's side away again: the helper, the commands and the hooks this setup wrote.
        The keys stay (the Buddy still holds their other halves) unless forget_keys is set."""
        self.say('Monitor Buddy: removing the helper from this computer' + (' (dry run)' if self.dry else ''))
        if self.which('systemctl') and not self.dry:
            self.run('systemctl', '--user', 'disable', '--now', UNIT)
        ours = [os.path.join(self.home, '.config', 'systemd', 'user', UNIT),
                os.path.join(self.home, '.local', 'share', 'applications', 'monitor-buddy.desktop')]
        ours += [os.path.join(self.home, '.config', browser, 'NativeMessagingHosts', ODOO_HOST + '.json') for browser in ('google-chrome', 'chromium')]
        link = os.path.join(self.home, '.local', 'bin', 'buddy-say')
        if os.path.islink(link) and 'buddy_say.py' in os.readlink(link):
            ours.append(link)
        fish = os.path.join(self.home, '.config', 'fish', 'conf.d', 'buddy.fish')
        try:
            with open(fish) as file:
                if file.read() == FISH_HOOK:
                    ours.append(fish)
        except OSError:
            pass
        for path in ours:
            if os.path.lexists(path):
                self.would(f'remove {path}') if self.dry else (os.remove(path), self.ok(f'removed {path}'))
        settings = os.path.realpath(os.path.join(self.home, '.claude', 'settings.json'))
        try:
            with open(settings) as file:
                data = json.load(file)
            hooks = data.get('hooks') if isinstance(data, dict) else None
        except (OSError, ValueError):
            hooks = None
        if isinstance(hooks, dict):
            changed = False
            for event in list(hooks):
                if isinstance(hooks[event], list):
                    kept = [entry for entry in hooks[event] if 'buddy_claude_hook.py' not in json.dumps(entry)]
                    changed |= len(kept) != len(hooks[event])
                    hooks[event] = kept
                    if not kept:
                        del hooks[event]
            if changed:
                if self.dry:
                    self.would(f'take the Monitor Buddy hooks out of {settings}')
                else:
                    self.write_file(settings, json.dumps(data, indent=2) + '\n', mode=0o600)
                    self.ok('Claude Code: the Monitor Buddy hooks are gone (the file as it was: settings.json.bak)')
        if self.which('systemctl') and not self.dry:
            self.run('systemctl', '--user', 'daemon-reload')
        if opened := self.state['done'].get('firewall'):
            rule = ['ufw', 'delete'] + self.firewall_rule(*opened.split())[1:]
            elevate = 'sudo' if self.interactive else 'pkexec'
            if self.dry:
                self.would(f'take the firewall rule for the Buddy\'s hello out ({opened})')
            elif self.which(elevate) and self.run(elevate, *rule, timeout=300)[0] == 0:
                del self.state['done']['firewall']
                self._save_state()
                self.ok('the firewall rule for the Buddy\'s hello is gone')
            else:
                self.info('the firewall rule for the Buddy\'s hello stays; to take it out:   sudo ' + shlex.join(rule))
        keys = os.path.join(self.home, '.config', 'monitor-buddy')
        if self.forget_keys and os.path.isdir(keys):
            self.would(f'delete the keys and answers in {keys}') if self.dry else (shutil.rmtree(keys), self.ok('the keys and the saved answers are deleted'))
        elif os.path.isdir(keys):
            self.info(f'the keys stay in {keys}, so a later setup pairs with the same Buddy again (--forget-keys deletes them)')
        self.say('Done.' if not self.dry else 'Dry run: nothing was changed.')
        return 0

    def summary(self):
        self.say('\n' + '─' * 60)
        if self.dry:
            self.say('Dry run: nothing was changed.')
        self.say('Works now:' if self.works else 'Nothing works yet; see below.')
        for line in self.works:
            self.say(f'  ✓ {line}')
        if self.left:
            self.say('\nLeft to do:')
            for number, (what, how) in enumerate(self.left, 1):
                self.say(f'  {number}. {what}\n     {how}')
            self.say('\nRun ./setup.sh again at any time: it skips what is done.')
        elif not self.dry:
            self.say('\nAll set. Updates later, over Wi-Fi:  python3 tools/buddy_flash.py monitor-buddy-vX.Y.Z-esp32c6-app.bin')

    def main(self, status_only=False):
        self.say('Monitor Buddy setup' + (' (dry run: nothing is changed)' if self.dry else ''))
        if status_only:
            self.dry = True
        try:
            self.check_python()
            self.make_keys()
            self.pair_board()
            self.install_service()
            self.install_say()
            self.install_app()
            self.look_at_desktop()
            self.optional_parts()
            self.check_link()
            self.allow_hellos()
        except Stop as stop:
            self.say(f'\n  ! {stop}')
            return 1
        finally:
            if self.service_was_stopped and not self.dry:   # never leave the helper off
                self.run('systemctl', '--user', 'start', UNIT)
        self.summary()
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--dry-run', action='store_true', help='only say what it would do')
    parser.add_argument('--status', action='store_true', help='only show what works and what is left')
    parser.add_argument('--yes', action='store_true', help='ask nothing')
    parser.add_argument('--ask-again', action='store_true', help='ask the optional questions again')
    parser.add_argument('--rotate-keys', action='store_true', help='replace both keys; the Buddy must be on USB')
    parser.add_argument('--port', help="the Buddy's USB port")
    parser.add_argument('--undo', action='store_true', help='remove the helper, commands and hooks from this computer again (keys stay)')
    parser.add_argument('--forget-keys', action='store_true', help='with --undo: also delete the keys')
    args = parser.parse_args()
    if sys.platform != 'linux':
        sys.exit('The laptop helper is for Linux. On Windows and macOS the Buddy still works by itself (face, clock, weather, '
                 f'GitHub, animations, game): install it from {INSTALLER}')
    if args.rotate_keys and (args.dry_run or args.status):
        sys.exit('--rotate-keys cannot be combined with --dry-run or --status.')
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    setup = Setup(dry=args.dry_run or args.status, yes=args.yes, ask_again=args.ask_again, rotate=args.rotate_keys, port=args.port)
    if args.undo:
        setup.forget_keys = args.forget_keys
        sys.exit(setup.undo())
    sys.exit(setup.main(status_only=args.status))


if __name__ == '__main__':
    main()
