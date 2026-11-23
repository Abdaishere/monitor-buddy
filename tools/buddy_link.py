#!/usr/bin/env python3
"""Tell a Monitor Buddy what this PC is doing, over Wi-Fi or else USB serial.

    python tools/buddy_link.py [PORT]

Wi-Fi: one UDP packet sealed with the link key in LINK_KEY_FILE, which
tools/buddy_setup.py makes and gives to the Buddy over USB (the format is
at serviceLink() in src/main.cpp), sent to the address in ADDRESS_FILE. The
Buddy prints its address on USB serial; this script saves it from there.

USB: "@MB STATE {...}" when the Buddy does not answer on Wi-Fi. Without PORT it
finds the Buddy by its USB name, so it reconnects when the Buddy is replugged
and comes back as another /dev/ttyACM number.

The state goes out when something changes and at least every 60 s (the Buddy
forgets a state older than 3 minutes):
  locked       the screen is locked
  mic          a program records from a microphone, seen on two polls in a row
  prs          my open GitHub pull requests (needs a signed-in gh)
  prs_failing  those updated in the last 4 days whose last commit's checks failed
  reviews      pull requests that wait for my review
  changes      mine with changes requested
  prs_merged   mine merged on the working day before (Friday on a Monday)
  prs_reviewed others' I reviewed that were updated on that day
  pr_list      the 4 updated last: {t: "#number title", s: 0 none, 1 passed,
               2 failed, 3 running, r: 1 changes requested, 2 approved}
  claude       1 a Claude Code session waits for me, 2 one just finished, else 0
  at           that session's project folder
  sessions     the open Claude Code sessions, the last changed first, at most 4:
               [folder, 0 working, 1 waiting for me or 2 done, since (UTC s)]
  use          [5-hour, 7-day] percent of Claude's limits, from the status
               line's cache while it is fresh
  use_reset    [when each resets], as the status line writes it
  in, out      the first unlock and the last lock of today's work day (05:00 to
               05:00) as HH:MM, '' before there is one; kept in DAYS_FILE
  yin, yout    the same of the work day before it in DAYS_FILE

Claude Code hooks write "needs FOLDER" or "done FOLDER" to CLAUDE_FILE and
delete it when I send a prompt. Each session also keeps "STATE FOLDER" in
SESSIONS_DIR/SESSION_ID, STATE being working, needs or done, until it ends.

Between rounds it pings the Buddy twice a second. The answers to pings and
states say what the Buddy asks for: a tap on MEETING IN n MIN opens that
meeting's link in the browser, and the BOOT button locks this PC.

Odoo: the summary the Chrome extension tools/chrome-odoo leaves in ODOO_FILE,
with the meetings of my Google calendars (buddy_calendar.py) added, goes out as
"O {...}" (USB: "@MB ODOO {...}") when it changes and every 60 s. The meetings'
links stay here.

Focus sync: while the Buddy's timer runs, desktop notifications are off and the
Caffeine extension is on, until the PC has been idle for half an hour. What
they were before is kept in FOCUS_FILE and put back when the timer stops or the
Buddy is out of reach for a minute.

It keeps the USB port open, so stop it before flashing:
    systemctl --user stop buddy-link
"""
import atexit
import csv
import glob
import json
import os
import re
import signal
import socket
import subprocess
import sys
import termios
import time
import tty
from datetime import date, datetime, timedelta, timezone

import buddy_calendar
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*'
CONFIG_H = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'config', 'config.h')
# The link key: 64 hex digits in a file only its owner can read. buddy_keys.py writes it.
LINK_KEY_FILE = os.path.expanduser('~/.config/monitor-buddy/link-key')
NO_LINK_KEY = 'This computer is not set up for the Buddy yet. Open Monitor Buddy and click Set up (or run ./setup.sh) first.'
ADDRESS_FILE = os.path.expanduser('~/.cache/monitor-buddy/address')
NO_ADDRESS = 'This computer does not know where the Buddy is yet. Plug it in by USB for a minute, then try again.'
LINK_PORT = 4747
LINK_TIMEOUT_S = 1
HELLO_SKEW_S = 60   # LINK_SKEW_S in src/main.cpp
POLL_S = 5
PING_S = 0.5
RESEND_S = 60
PRS_EVERY_S = 600
FAILING_MAX_AGE = timedelta(days=4)
CLAUDE_FILE = os.path.expanduser('~/.cache/monitor-buddy/claude')
CLAUDE_MAX_AGE_S = {'needs': 1800, 'done': 90}
SESSIONS_DIR = os.path.expanduser('~/.cache/monitor-buddy/sessions')
SESSION_MAX_AGE_S = 3 * 3600
SESSION_STATES = {'working': 0, 'needs': 1, 'done': 2}
JOIN_BEFORE_S = 600   # a tap joins a meeting that starts within this or is under way
USAGE_FILE = os.path.expanduser('~/.cache/claude-statusline.work')
USAGE_MAX_AGE_S = 5 * 3600
FOCUS_FILE = os.path.expanduser('~/.cache/monitor-buddy/focus')
ODOO_FILE = os.path.expanduser('~/.cache/monitor-buddy/odoo.json')
ODOO_MAX_AGE_S = 30 * 60
PACKET_MAX = 1300   # the Buddy takes packets of up to 1400 bytes, seal included
DAYS_FILE = os.path.expanduser('~/.local/share/monitor-buddy/days.csv')
DAY_STARTS = timedelta(hours=5)   # a work day runs from 05:00 to 05:00
SLEPT = timedelta(minutes=1)      # longer between two looks at the lock: the PC was asleep
BANNERS = ('org.gnome.desktop.notifications', 'show-banners')
CAFFEINE = ('org.gnome.shell.extensions.caffeine', 'cli-toggle')
CAFFEINE_SCHEMAS = os.path.expanduser('~/.local/share/gnome-shell/extensions/caffeine@patapon.info/schemas')
CAFFEINE_IDLE_MAX_MS = 30 * 60 * 1000   # an unattended PC gets its screen lock back
OUT_OF_REACH_S = 60
CHECKS = {'SUCCESS': 1, 'FAILURE': 2, 'ERROR': 2, 'PENDING': 3, 'EXPECTED': 3}
REVIEWS = {'CHANGES_REQUESTED': 1, 'APPROVED': 2}
PRS_QUERY = (
    '{viewer{pullRequests(states:OPEN,first:50){nodes{number title updatedAt reviewDecision commits(last:1){nodes{'
    'commit{statusCheckRollup{state}}}}}}}'
    'search(query:"is:open is:pr review-requested:@me archived:false",type:ISSUE){issueCount}'
    'merged:search(query:"is:pr author:@me is:merged merged:%(day)s",type:ISSUE){issueCount}'
    'reviewed:search(query:"is:pr reviewed-by:@me -author:@me updated:%(day)s",type:ISSUE){issueCount}}'
)


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=30).stdout


def display_session():
    """logind's id of my graphical session, '' without one."""
    return run('loginctl', 'show-user', str(os.getuid()), '-p', 'Display', '--value').strip()


def locked():
    """logind's LockedHint of my graphical session. GNOME's ScreenSaver.GetActive
    is not used: it stays false while the screen is locked by the shortcut."""
    session = display_session()
    return bool(session) and run('loginctl', 'show-session', session, '-p', 'LockedHint', '--value').strip() == 'yes'


def work_day(moment):
    """The work day a local time belongs to, as YYYY-MM-DD."""
    return (moment - DAY_STARTS).date().isoformat()


def read_days():
    """{work day: [arrived, left]} from DAYS_FILE, times as HH:MM or ''."""
    try:
        with open(DAYS_FILE, newline='') as file:
            return {row[0]: (row[1:] + ['', ''])[:2] for row in list(csv.reader(file))[1:] if row}
    except (OSError, csv.Error):
        return {}


def write_days(days):
    os.makedirs(os.path.dirname(DAYS_FILE), mode=0o700, exist_ok=True)
    part = DAYS_FILE + '.part'
    with os.fdopen(os.open(part, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), 'w', newline='') as file:
        csv.writer(file, lineterminator='\n').writerows(
            [['date', 'arrived', 'left'], *([day, *times] for day, times in sorted(days.items()))])
    os.replace(part, DAYS_FILE)


def note_lock(days, look, is_locked, now):
    """Keep the first unlock and the last lock of each work day in `days` and
    DAYS_FILE. `look` is the look before this one, (locked, when), or None."""
    if not is_locked:
        times = days.setdefault(work_day(now), ['', ''])
        if not times[0]:
            times[0] = now.strftime('%H:%M')
            write_days(days)
    elif look and not look[0]:   # a lock seen happening
        at = now if now - look[1] <= SLEPT else look[1]   # asleep since the last look: it locked as it went to sleep
        days.setdefault(work_day(at), ['', ''])[1] = at.strftime('%H:%M')
        write_days(days)


def day_times(days, now):
    """[in, out] of the current work day, then of the one before it in `days`."""
    today = work_day(now)
    before = max((day for day in days if day < today), default=None)
    return days.get(today, ['', '']) + days.get(before, ['', ''])


def day_before(today):
    """The working day before `today`: Friday for a Saturday, a Sunday or a Monday."""
    return today - timedelta(days={0: 3, 6: 2}.get(today.weekday(), 1))


def pull_requests(day):
    """(open, failing, pr_list, reviews, changes, merged on `day`, reviewed on
    `day`), or None when gh did not answer."""
    start = datetime.combine(day, datetime.min.time()).astimezone()   # that day's local midnight
    span = f'{start.isoformat()}..{(start + timedelta(days=1, seconds=-1)).isoformat()}'
    try:
        data = json.loads(run('gh', 'api', 'graphql', '-f', f'query={PRS_QUERY % {"day": span}}'))['data']
        nodes, reviews = data['viewer']['pullRequests']['nodes'], data['search']['issueCount']
        merged, reviewed = data['merged']['issueCount'], data['reviewed']['issueCount']
    except (ValueError, KeyError, TypeError, subprocess.TimeoutExpired):
        return None
    since = (datetime.now(timezone.utc) - FAILING_MAX_AGE).strftime('%Y-%m-%dT%H:%M:%SZ')
    for node in nodes:
        node['check'] = CHECKS.get((node['commits']['nodes'][0]['commit']['statusCheckRollup'] or {}).get('state'), 0)
    failing = sum(node['updatedAt'] >= since and node['check'] == 2 for node in nodes)
    newest = sorted(nodes, key=lambda node: node['updatedAt'], reverse=True)[:4]
    # The [IMP][task] tags at the start say less than the name after them: they go.
    pr_list = [{'t': f"#{node['number']} {re.sub(r'^(\[[^]]*\]\s*)+', '', node['title'])}"[:50], 's': node['check'],
                'r': REVIEWS.get(node['reviewDecision'], 0)} for node in newest]
    changes = sum(node['reviewDecision'] == 'CHANGES_REQUESTED' for node in nodes)
    return len(nodes), failing, pr_list, reviews, changes, merged, reviewed


def claude():
    """(1 a session waits for me, 2 one just finished, else 0; its project folder)."""
    try:
        word, _, project = open(CLAUDE_FILE).read().strip().partition(' ')
        age = time.time() - os.path.getmtime(CLAUDE_FILE)
    except OSError:
        return 0, ''
    state = {'needs': 1, 'done': 2}.get(word, 0) if age <= CLAUDE_MAX_AGE_S.get(word, 0) else 0
    return state, project[:23] if state else ''


def sessions():
    """The open Claude Code sessions, the last changed first, at most 4: [folder,
    0 working, 1 waiting for me or 2 done, since (UTC seconds)]."""
    found = []
    for path in glob.glob(os.path.join(SESSIONS_DIR, '*')):
        try:
            word, _, project = open(path).read().strip().partition(' ')
            since = int(os.path.getmtime(path))
        except OSError:
            continue
        if word in SESSION_STATES and time.time() - since <= SESSION_MAX_AGE_S:
            found.append([project[:23], SESSION_STATES[word], since])
    return sorted(found, key=lambda session: session[2], reverse=True)[:4]


def usage():
    """([5-hour, 7-day] percent of Claude's limits, [when each resets]), or None
    when the status line's cache is old."""
    try:
        five, five_reset, week, week_reset, stamp = open(USAGE_FILE).read().split()
        if time.time() - int(stamp) <= USAGE_MAX_AGE_S:
            return [int(five), int(week)], [five_reset.strip('-'), week_reset.strip('-')]   # "-": not known
    except (OSError, ValueError):
        pass
    return None


_calendar = {'at': 0.0, 'meetings': []}


def google_meetings():
    """The next 24 h of my Google calendars, read again at most once a minute."""
    if time.monotonic() - _calendar['at'] >= 60:
        try:
            _calendar['meetings'] = buddy_calendar.meetings()
        except (ValueError, KeyError, OSError) as error:   # one odd event must not stop the link
            print('calendar:', error, flush=True)
        _calendar['at'] = time.monotonic()
    return _calendar['meetings']


def odoo():
    """(the summary in ODOO_FILE as one compact line, or None; why there is none;
    the meetings as [name, start, stop, link]). Long lists lose their last
    entries to fit one packet."""
    try:
        if time.time() - os.path.getmtime(ODOO_FILE) > ODOO_MAX_AGE_S:
            return None, 'no news from Chrome for 30 minutes', []
        with open(ODOO_FILE) as file:
            data = json.load(file)
    except (OSError, ValueError):
        return None, 'nothing from Chrome yet', []
    if 'error' in data:
        return None, str(data['error']), []
    both = {}
    for name, start, stop, *link in data.get('m', []) + google_meetings():
        meeting = both.setdefault((name, start), [name, start, stop, ''])
        meeting[3] = meeting[3] or (link[0] if link else '')
    meetings = sorted(both.values(), key=lambda meeting: meeting[1])
    data['m'] = [meeting[:3] for meeting in meetings[:3]]
    while True:
        line = json.dumps(data, separators=(',', ':'))
        if len(line) <= PACKET_MAX or not (data.get('t') or data.get('y') or data.get('m')):
            return line, None, meetings
        (data.get('t') or data.get('y') or data.get('m')).pop()


def fit(state):
    """Drop the last pull requests, then the last sessions, until `state` fits one packet."""
    while len(json.dumps(state)) > PACKET_MAX and (state['pr_list'] or state['sessions']):
        (state['pr_list'] or state['sessions']).pop()


def meeting_to_join(meetings, now):
    """The meeting a tap on MEETING IN n MIN means: the first to start within
    JOIN_BEFORE_S (or that began under 90 s ago), else one under way; None
    without one. One with a link wins over its twin from the other calendar."""
    near = [meeting for meeting in meetings if meeting[1] <= now + JOIN_BEFORE_S and meeting[2] > now]
    return min(near, key=lambda meeting: (meeting[1] < now - 90, meeting[1], not meeting[3]), default=None)


def act(answer, meetings):
    """Do what the Buddy asks in the status byte of its answer to a ping or a
    state: bit 2, open the meeting's link; bit 3, lock this PC."""
    if answer is None or len(answer) < 9:
        return
    try:
        if answer[8] & 4:
            meeting = meeting_to_join(meetings, time.time())
            if not meeting:
                print('join: no meeting within 10 minutes', flush=True)
            elif meeting[3].startswith('https://'):   # whoever sent the invitation wrote the link
                subprocess.Popen(['xdg-open', meeting[3]], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                print('join:', meeting[0], flush=True)
            else:
                print('join:', meeting[0], 'has no https link', flush=True)
        if answer[8] & 8 and (session := display_session()):
            run('loginctl', 'lock-session', session)
            print('lock: the BOOT button', flush=True)
    except (OSError, subprocess.TimeoutExpired) as error:
        print('could not do what the Buddy asked:', error, flush=True)


def mic():
    """A program records from a microphone. Level meters do not count."""
    try:
        nodes = json.loads(run('pw-dump'))
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return False
    for node in nodes:
        info = node.get('info') or {}
        props = info.get('props') or {}
        if (props.get('media.class') == 'Stream/Input/Audio' and info.get('state') == 'running'
                and props.get('media.name') != 'Peak detect' and not props.get('stream.monitor')):
            return True
    return False


def setting(key, value=None):
    """Read the gsettings key BANNERS or CAFFEINE, or write it when a value is given."""
    where = ('--schemadir', CAFFEINE_SCHEMAS) if key is CAFFEINE else ()
    return run('gsettings', *where, *(('set', *key, value) if value else ('get', *key))).strip()


def focus_sync(focused, runs):
    """Follow the Buddy's timer: notifications off and Caffeine on while it runs.
    Returns whether the PC is in focus now."""
    if runs and not focused:
        with open(FOCUS_FILE, 'w') as file:
            json.dump({'banners': setting(BANNERS), 'caffeine': setting(CAFFEINE)}, file)
        setting(BANNERS, 'false')
        print('focus on', flush=True)
    elif focused and not runs:
        with open(FOCUS_FILE) as file:
            before = json.load(file)
        if before['caffeine'] != 'true':
            setting(CAFFEINE, 'false')
        setting(BANNERS, before['banners'] if before['banners'] in ('true', 'false') else 'true')
        os.remove(FOCUS_FILE)
        print('focus off', flush=True)
    if runs:
        idle = re.search(r'\d+', run('gdbus', 'call', '--session', '--dest', 'org.gnome.Mutter.IdleMonitor', '--object-path',
                                      '/org/gnome/Mutter/IdleMonitor/Core', '--method', 'org.gnome.Mutter.IdleMonitor.GetIdletime'))
        awake = 'true' if idle and int(idle[0]) < CAFFEINE_IDLE_MAX_MS else 'false'
        with open(FOCUS_FILE) as file:
            mine = json.load(file)['caffeine'] != 'true'   # it was already on: it is not mine to switch
        if mine and setting(CAFFEINE) != awake:
            setting(CAFFEINE, awake)
    return runs


def link_key():
    """The link key as 64 hex digits, or None: from LINK_KEY_FILE, else (a build
    of your own with the key compiled in) from LINK_KEY in config/config.h."""
    try:
        with open(LINK_KEY_FILE) as file:
            key = file.read().strip()
        if re.fullmatch(r'[0-9a-fA-F]{64}', key):
            return key
    except OSError:
        pass
    try:
        with open(CONFIG_H) as file:
            key = re.search(r'^\s*#define\s+LINK_KEY\s+"([0-9a-fA-F]{64})"', file.read(), re.M)
    except OSError:
        return None
    return key[1] if key else None


def link_cipher():
    """The Wi-Fi link's cipher, or None when this computer has no link key."""
    key = link_key()
    return AESGCM(bytes.fromhex(key)) if key else None


def send_wifi(cipher, address, message, timeout=LINK_TIMEOUT_S):
    """Send one sealed packet. Returns the Buddy's sealed answer, or None without
    one: the 8 bytes that name its firmware build, then a status byte whose
    bit 0 says its timer runs and bits 2 and 3 what it asks for (act())."""
    nonce = int(time.time() * 1000).to_bytes(8, 'big') + os.urandom(4)
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as link:
            link.settimeout(timeout)
            link.sendto(nonce + cipher.encrypt(nonce, message, None), (address, LINK_PORT))
            answer = cipher.decrypt(bytes([nonce[0] | 0x80]) + nonce[1:], link.recv(2048), None)
    except (OSError, InvalidTag):
        return None
    return answer[2:] if answer.startswith(b'ok') else None


def hello_address(cipher, data, sender, now=None):
    """`sender` when `data` is a hello from the Buddy (linkHello() in
    src/main.cpp): sealed with the link key, naming that same address, and at
    most HELLO_SKEW_S old, so a recorded one cannot steer the helper. Else None."""
    if len(data) < 12 + 1 + 16 or data[0] != 0x40:
        return None
    try:
        words = cipher.decrypt(data[:12], data[12:], None).decode().split()
    except (InvalidTag, UnicodeDecodeError):
        return None
    now = time.time() if now is None else now
    if len(words) == 3 and words[0] == 'hello' and words[1] == sender and words[2].isdigit() \
            and abs(now - int(words[2])) <= HELLO_SKEW_S:
        return sender
    return None


def hello_socket():
    """The socket the Buddy's hellos arrive on, or None when the port is taken."""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('', LINK_PORT))
    except OSError as error:
        print('hellos: cannot listen on UDP', LINK_PORT, error, flush=True)
        return None
    sock.setblocking(False)
    return sock


def heard_hello(sock, cipher):
    """The address of the last good hello waiting on `sock`, else None."""
    found = None
    while sock and cipher:
        try:
            data, (sender, _) = sock.recvfrom(2048)
        except OSError:
            break
        found = hello_address(cipher, data, sender) or found
    return found


def save_address(address):
    os.makedirs(os.path.dirname(ADDRESS_FILE), exist_ok=True)
    with open(ADDRESS_FILE, 'w') as file:
        file.write(address)
    print('buddy address:', address, flush=True)


def open_port():
    fd = os.open((glob.glob(PORT) or [PORT])[0], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[2] &= ~termios.HUPCL   # closing must not pulse DTR/RTS: that restarts the board
    attrs[4] = attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def lost(fd, error):
    """Close a dead port, so the kernel frees its name, and report it once."""
    if fd is not None:
        print('lost the Buddy:', error, flush=True)
        try:
            os.close(fd)
        except OSError:
            pass
    return None


def main():
    # Stopped or crashed in focus: notifications and Caffeine go back as they were.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit())
    atexit.register(lambda: os.path.exists(FOCUS_FILE) and focus_sync(True, False))
    fd, cipher, route, hellos = None, link_cipher(), None, hello_socket()
    try:
        address = open(ADDRESS_FILE).read().strip()
    except OSError:
        address = ''
    prs, prs_at, prs_day, sent, sent_at, log, is_locked = (0, 0, [], 0, 0, 0, 0), 0.0, None, None, 0.0, b'', False
    mic_before, timer_runs, heard_at, focused = False, False, 0.0, os.path.exists(FOCUS_FILE)
    odoo_sent, odoo_sent_at, odoo_said, meetings = None, 0.0, '', []
    days, look = read_days(), None   # a restart keeps the day's arrival
    while True:
        now = time.monotonic()
        before = day_before(date.today())
        if now - prs_at >= PRS_EVERY_S or not prs_at or before != prs_day:   # a new day counts its own day before
            prs, prs_at, prs_day = pull_requests(before) or prs, now, before
        try:
            is_locked = locked()
        except subprocess.TimeoutExpired:   # loginctl hung: keep the last answer
            pass
        else:
            seen = datetime.now()
            note_lock(days, look, is_locked, seen)
            look = is_locked, seen
        mic_now = mic()
        claude_state, project = claude()
        state = {'locked': is_locked, 'mic': mic_now and mic_before, 'prs': prs[0], 'prs_failing': prs[1], 'reviews': prs[3],
                 'changes': prs[4], 'prs_merged': prs[5], 'prs_reviewed': prs[6], 'claude': claude_state, 'at': project,
                 'sessions': sessions(), 'pr_list': list(prs[2])}
        state['in'], state['out'], state['yin'], state['yout'] = day_times(days, datetime.now())
        mic_before = mic_now
        if used := usage():
            state['use'], state['use_reset'] = used
        fit(state)
        try:
            if fd is None:
                fd, sent = open_port(), None
                print('connected', flush=True)
            while chunk := os.read(fd, 4096):   # drain the Buddy's log so its buffer never fills
                log += chunk
            raise OSError('port closed')        # a read of nothing: the Buddy was unplugged
        except BlockingIOError:
            pass
        except OSError as error:
            fd = lost(fd, error)
        *lines, log = log[-8192:].split(b'\n')
        for line in lines:   # the Buddy's mood changes go to the journal
            if line.startswith((b'mood:', b'knock:')):
                print('buddy', line.decode(errors='replace').strip(), flush=True)
            if found := re.search(rb' timer=([01])', line):
                timer_runs, heard_at = found[1] == b'1', now
            if (found := re.search(rb' ip=(\d+\.\d+\.\d+\.\d+)', line)) and found[1].decode() not in (address, '0.0.0.0'):
                address = found[1].decode()
                save_address(address)
        answer = None
        if state != sent or now - sent_at >= RESEND_S:
            line, was = json.dumps(state), route
            answer = send_wifi(cipher, address, line.encode()) if cipher and address else None
            route = 'wifi' if answer is not None else None
            if not route and fd is not None:
                try:
                    os.write(fd, f'@MB STATE {line}\n'.encode())
                    route = 'usb'
                except OSError as error:
                    fd = lost(fd, error)
            if route:
                if state != sent or route != was:
                    print('sent', route, line, flush=True)
                sent, sent_at = state, now
        elif cipher and address:
            answer = send_wifi(cipher, address, b'P')   # between states: is the timer running?
        if answer is not None and len(answer) > 8:
            timer_runs, heard_at = bool(answer[8] & 1), now
        elif now - heard_at > OUT_OF_REACH_S:
            timer_runs = False
        act(answer, meetings)
        odoo_line, odoo_trouble, meetings = odoo()
        if odoo_trouble != odoo_said:
            print('odoo:', odoo_trouble or 'data from Chrome', flush=True)
            odoo_said = odoo_trouble
        if odoo_line and (odoo_line != odoo_sent or now - odoo_sent_at >= RESEND_S):
            delivered = bool(cipher and address) and send_wifi(cipher, address, b'O' + odoo_line.encode()) is not None
            if not delivered and fd is not None:
                try:
                    os.write(fd, f'@MB ODOO {odoo_line}\n'.encode())
                    delivered = True
                except OSError as error:
                    fd = lost(fd, error)
            if delivered:
                odoo_sent, odoo_sent_at = odoo_line, now
        try:
            focused = focus_sync(focused, timer_runs)
        except (OSError, ValueError, KeyError, subprocess.TimeoutExpired) as error:
            print('focus sync failed:', error, flush=True)
        while (left := now + POLL_S - time.monotonic()) > 0:   # pings, so a tap or a BOOT press is answered at once
            time.sleep(min(PING_S, left))
            if (moved := heard_hello(hellos, cipher)) and moved != address:   # the Buddy got a new address
                address = moved
                save_address(address)
            if cipher and address:
                act(send_wifi(cipher, address, b'P'), meetings)


if __name__ == '__main__':
    main()
