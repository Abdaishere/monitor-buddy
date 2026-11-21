#!/usr/bin/env python3
"""A whole simulated workday on a Monitor Buddy, from power-on to the next morning.

    python3 workday.py [OUT_DIR]     # contact sheet, video and timeline of the day

`run_workday(buddy, watch)` plays Monday 2026-01-05 with the clock warped: the
laptop's feed (what tools/buddy_link.py would send every minute), fingers,
knocks, a focus session, a meeting, CI, Claude Code, lunch, the end of the day,
the night and the next morning. At every moment worth a look it calls
`watch(name, caption, seconds)`. The test in tests/test_workday.py checks each
of those moments; this script films them.
"""
import os
import re
import subprocess
import sys
from datetime import timedelta

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from mbsim import MONDAY_8AM, Buddy  # noqa: E402
from mock_internet import MockInternet  # noqa: E402

START = MONDAY_8AM.replace(hour=7, minute=55)


class Laptop:
    """Plays tools/buddy_link.py: a STATE and an Odoo packet every minute."""

    def __init__(self, buddy):
        self.b = buddy
        self.on = False            # the laptop is awake and buddy-link runs
        self.pc = {'locked': False, 'mic': False, 'prs': 3, 'prs_failing': 0, 'reviews': 2, 'changes': 1,
                   'prs_merged': 2, 'prs_reviewed': 4, 'claude': 0, 'at': '',
                   'pr_list': [{'t': '#4821 Fix the invoice export', 's': 1, 'r': 2},
                               {'t': '#4790 Stock move rounding', 's': 3, 'r': 0},
                               {'t': '#4702 Upgrade script for 19.0', 's': 1, 'r': 1}],
                   'sessions': [], 'use': [12, 58], 'use_reset': ['13:10', 'Sat'],
                   'in': '', 'out': '', 'yin': '08:12', 'yout': '17:48'}
        self.hours = 0.0
        self.before = 7.75         # hours logged on the working day before
        self.before_day = 5        # that day's weekday: 1 Monday .. 5 Friday (Monday's 'yesterday' is Friday)
        self.meetings = []         # [name, start, stop] in UTC seconds
        self.dev = False           # a task sits in a development stage
        self.asked = []            # what the Buddy asked the laptop to do: 'join', 'lock'
        self.unanswered = 0

    def odoo_packet(self):
        return {'h': round(self.hours, 2), 'hb': self.before, 'bw': self.before_day,
                'y': ['Fix the invoice export', 'Review stock moves'], 'o': 7, 'r': 2, 'd': 1 if self.dev else 0, 'a': 3,
                't': [[48211, 'Invoice export drops the tax line', 'Development (Team)' if self.dev else 'Code Review'],
                      [48190, 'Stock move rounding', 'Code Review'], [47999, 'Upgrade script for 19.0', 'To Do']],
                'm': [m for m in self.meetings if m[2] > self.b.true_ms // 1000][:3]}

    def send(self):
        """One round of buddy_link: the state, then Odoo. Notes what the Buddy asks for."""
        if not self.on:
            return
        import json
        answer = self.b.link(json.dumps(self.pc, separators=(',', ':')))
        if answer is None:
            self.unanswered += 1
        else:
            if answer[8] & 4:
                self.asked.append('join')
            if answer[8] & 8:
                self.asked.append('lock')
        self.b.odoo(**self.odoo_packet())

    def set(self, **changes):
        self.pc.update(changes)
        self.send()

    def meeting(self, name, start, minutes):
        at = int(start.timestamp())
        self.meetings.append([name, at, at + minutes * 60])
        self.meetings.sort(key=lambda m: m[1])

    def until(self, when):
        """Let the day run to `when` ('HH:MM'), the feed going out every minute."""
        hour, minute = map(int, when.split(':'))
        target = self.b.now.replace(hour=hour, minute=minute, second=0, microsecond=0)
        if target <= self.b.now:
            target += timedelta(days=1)
        while self.b.now < target:
            self.b.run(min(60, (target - self.b.now).total_seconds()))
            self.send()


def run_workday(b, watch, laptop=None):
    """Plays the day on Buddy `b` (booted at START). Returns the Laptop."""
    pc = laptop or Laptop(b)

    # ── 07:55 power on ───────────────────────────────────────
    b.run(3)
    watch('boot_hold_still', '07:55 power on: measuring the rest position', 2)
    b.run(4)
    watch('boot_connecting', '07:55 joining the saved Wi-Fi', 3)
    b.run(8)
    watch('boot_face', '07:55 on Wi-Fi, clock set over NTP', 3)
    b.run(20)
    watch('morning_pages', '07:56 the pages flip by themselves: clock, date, weather ...', 26)

    # ── 08:05 the laptop is unlocked ─────────────────────────
    pc.until('08:05')
    pc.on = True
    pc.hours = 0.0
    pc.set(**{'in': '08:05'})
    b.run(1.5)
    watch('briefing_yesterday', '08:05 laptop unlocked: first the day before', 4)
    b.run(12)
    watch('briefing_today', '08:05 ... then GOOD MORNING with today', 4)
    b.run(18)

    # ── 08:10 knock knock ────────────────────────────────────
    pc.until('08:10')
    b.show_page(0)
    b.run(1)
    b.double_knock()
    watch('knock_hello', '08:10 knock, knock on its head: a greeting', 2)
    b.run(1.5)
    watch('knock_today', '08:10 ... then the Today page', 3)

    # ── 08:30 focus: lay it flat ─────────────────────────────
    pc.until('08:30')
    pc.dev = True
    pc.send()
    b.show_page(0)
    b.run(1)
    watch('locked_in', '08:30 a task in Development: LOCKED IN', 3)
    b.lay_flat()
    b.run(4)
    watch('focus_start', '08:30 laid flat: the focus timer starts', 4)
    pc.until('08:58')
    watch('focus_28min', '08:58 28 minutes of focus', 3)
    b.stand_up()
    b.run(1.2)
    watch('focus_done_gif', '08:58 stood up after 25 min or more: the focus-done animation', 6)
    b.run(1.5)
    watch('focus_result', '08:58 FOCUSED 28 min', 3)
    pc.hours = 0.75

    # ── 09:20 Claude Code ────────────────────────────────────
    pc.until('09:20')
    now = b.true_ms // 1000
    pc.set(sessions=[['monitor-buddy', 0, now - 500]])
    pc.until('09:24')
    now = b.true_ms // 1000
    b.show_page(0)
    pc.set(claude=1, at='monitor-buddy', sessions=[['monitor-buddy', 1, now]])
    b.run(1.5)
    watch('claude_needs_you', '09:24 a Claude Code session waits for an answer', 5)
    pc.until('09:26')
    now = b.true_ms // 1000
    b.show_page(0)
    pc.set(claude=2, at='monitor-buddy', sessions=[['monitor-buddy', 2, now]])
    b.run(1.5)
    watch('claude_done', '09:26 Claude is done', 4)
    pc.until('09:28')
    pc.set(claude=0, at='')
    b.show_page(14)
    b.run(1)
    watch('claude_page', '09:28 the Claude page: limits and sessions', 3)

    # ── 09:40 CI ─────────────────────────────────────────────
    pc.until('09:40')
    pc.pc['pr_list'][0]['s'] = 2
    b.show_page(0)
    pc.set(prs_failing=1)
    b.run(1.5)
    watch('ci_red', '09:40 a pull request fails its checks: CI RED', 4)
    b.show_page(10)
    b.run(1)
    watch('prs_page', '09:41 the pull requests page', 3)
    pc.until('09:55')
    pc.pc['pr_list'][0]['s'] = 1
    b.show_page(0)
    b.run(0.5)
    pc.set(prs_failing=0)
    b.run(1.0)
    watch('ci_green_gif', '09:55 the last failing check passes: ALL GREEN', 6)
    pc.hours = 1.5

    # ── 10:30 a meeting ──────────────────────────────────────
    pc.meeting('Weekly sync', b.now.replace(hour=10, minute=30, second=0, microsecond=0), 30)
    pc.meeting('Design review', b.now.replace(hour=15, minute=0, second=0, microsecond=0), 45)
    pc.until('10:26')
    b.show_page(0)
    b.run(1.5)
    watch('meeting_soon', '10:26 MEETING IN 4 MIN, with its name', 5)
    b.tap()
    watch('meeting_join', '10:26 a tap: the laptop opens the meeting link', 3)
    pc.send()
    pc.until('10:30')
    b.run(5)
    watch('meeting_now', '10:30 MEETING NOW', 3)
    pc.set(mic=True)
    b.run(1.5)
    watch('on_air_start', '10:30 the microphone is on: ON AIR', 3)
    pc.until('10:58')
    watch('on_air', '10:58 in the meeting for 28 minutes', 3)
    pc.set(mic=False)
    pc.hours = 2.5

    # ── 11:05 a note from the shell ──────────────────────────
    pc.until('11:05')
    b.show_page(9)
    b.run(1)
    watch('odoo_page', '11:05 the Odoo page: hours, tasks', 3)
    b.say('make test: 212 passed in 3m41s')
    b.run(1)
    watch('notice', '11:05 buddy-say: a long command ended', 4)
    b.tap()

    # ── 12:15 lunch ──────────────────────────────────────────
    pc.until('12:15')
    pc.hours = 3.5
    pc.set(locked=True)
    b.run(2)
    watch('lunch', '12:15 laptop locked around noon: LUNCH BREAK', 5)
    pc.until('13:00')
    pc.set(locked=False)
    b.run(2)
    watch('back_from_lunch', '13:00 back', 2)

    # ── 13:30 brightness, pages by hand ──────────────────────
    pc.until('13:30')
    b.show_page(3)
    b.run(1)
    b.swipe_down()
    watch('brightness', '13:30 a swipe down dims the screen', 2)
    b.swipe_up()
    b.swipe_left()
    watch('swipe', '13:30 a swipe to the next page', 2)

    # ── 14:20 away, long enough for the screensaver ──────────
    pc.until('14:20')
    pc.hours = 4.75
    pc.set(locked=True)
    b.run(2)
    watch('away', '14:20 laptop locked: AWAY', 4)
    pc.until('14:51')
    watch('screensaver', '14:51 away for 30 minutes: the screensaver', 6)
    pc.until('14:56')
    pc.set(locked=False)
    b.run(2)

    # ── 15:00 second meeting, then a shake ───────────────────
    pc.until('15:00')
    pc.set(mic=True)
    pc.until('15:40')
    pc.set(mic=False)
    pc.hours = 6.5
    pc.until('15:50')
    b.show_page(0)
    b.run(1)
    b.shake()
    b.run(0.3)
    watch('dizzy', '15:50 shaken: WHOAAA', 0.8)       # dizzy for 1.3 s after the last jolt
    b.run(0.6)
    watch('really', '15:50 ... REALLY?', 1.5)          # the look, for 2.4 s
    b.run(1.2)
    watch('shake_today', '15:50 ... and the Today page', 3)
    b.run(10)

    # ── 16:40 the day's hours are in ─────────────────────────
    pc.until('16:40')
    pc.hours = 7.9
    pc.send()
    b.show_page(9)
    b.run(1)
    pc.hours = 8.1
    pc.send()
    b.run(1.2)
    watch('full_day_gif', '16:40 eight hours logged: the full-day animation', 6)
    b.run(6)
    b.show_page(12)
    b.run(1)
    watch('today_page', '16:41 the Today page by hand', 3)

    # ── 17:30 the end of the day ─────────────────────────────
    pc.until('17:30')
    b.show_page(1)
    b.run(1)
    pc.set(locked=True, out='17:30')
    b.run(1.5)
    watch('day_done', '17:30 laptop locked after 17:00: DAY DONE', 6)
    b.run(32)
    watch('evening_away', '17:31 ... and AWAY for the evening', 3)
    pc.until('17:36')
    pc.on = False                # the laptop sleeps: no more feed

    # ── the evening and the night ────────────────────────────
    b.run_until('18:10')
    watch('evening_pages', '18:10 no laptop: the pages flip on', 16)
    b.run_until('22:59')
    b.show_page(0)
    b.run(50)
    watch('before_night', '22:59 a minute before the night window', 2)
    b.run(25)
    watch('night', '23:00 night: dimmed and sleepy', 4)
    b.run_until('23:30')
    b.tap()
    watch('night_wake', '23:30 a tap lights it up for 30 s', 3)
    b.run(40)
    watch('night_again', '23:31 ... and it dims again', 2)
    b.run_until('03:00')
    watch('night_3am', '03:00 still asleep', 2)
    b.run_until('06:59')
    b.run(50)
    b.show_page(0)
    b.run(25)
    watch('morning', '07:00 the night is over', 3)

    # ── the next morning ─────────────────────────────────────
    b.run_until('08:02')
    pc.on = True
    pc.hours = 0.0
    pc.before = 8.1
    pc.before_day = 1
    pc.pc.update(locked=False, out='', yin='08:05', yout='17:30', prs_merged=1, prs_reviewed=2)
    pc.pc['in'] = '08:02'
    pc.send()
    b.run(1.5)
    watch('tuesday_yesterday', 'Tuesday 08:02: Monday at a glance', 4)
    b.run(13)
    watch('tuesday_today', 'Tuesday 08:02: GOOD MORNING', 4)
    return pc


# ── filming ───────────────────────────────────────────────────
def film(out, fps=10, scale=3):
    """Runs the day and writes OUT/workday.mp4, workday-contact-sheet.png and workday-timeline.md."""
    from PIL import Image, ImageDraw, ImageFont
    os.makedirs(out, exist_ok=True)
    frames = os.path.join(out, 'frames')
    os.makedirs(frames, exist_ok=True)
    for name in os.listdir(frames):
        os.remove(os.path.join(frames, name))
    try:
        font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 22)
        small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 13)
    except OSError:
        font = small = ImageFont.load_default()
    width, height, bar = 320 * scale, 172 * scale, 56
    count = [0]
    stills, timeline = [], []

    def frame(shot, caption, dim):
        canvas = Image.new('RGB', (width, height + bar), (18, 18, 22))
        big = shot.resize((width, height), Image.NEAREST)
        if dim < 1.0:             # the backlight: a night screen is dark in the room, too
            big = Image.eval(big, lambda v: int(v * dim))
        canvas.paste(big, (0, 0))
        ImageDraw.Draw(canvas).text((14, height + 14), caption, fill=(235, 235, 240), font=font)
        canvas.save(os.path.join(frames, 'f%06d.png' % count[0]))
        count[0] += 1

    mock = MockInternet().start()
    with Buddy(when=START, internet=mock) as b:
        def watch(name, caption, seconds):
            light = b.board()['backlight']
            dim = 0.25 + 0.75 * min(light, 180) / 180
            stills.append((name, caption, b.shot(), dim))
            timeline.append((b.now.strftime('%a %H:%M:%S'), name, caption, light, b.status_line().get('app', '?')))
            for _ in range(int(seconds * fps)):
                frame(b.shot(), caption, dim)
                b.run_ms(1000 // fps)

        laptop = run_workday(b, watch)
        log = b.log
    mock.stop()

    video = os.path.join(out, 'workday.mp4')
    subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-framerate', str(fps), '-i', os.path.join(frames, 'f%06d.png'),
                    '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '20', '-movflags', '+faststart', video], check=True)
    for name in os.listdir(frames):
        os.remove(os.path.join(frames, name))
    os.rmdir(frames)

    columns, cell_w, cell_h, label = 6, 320, 172, 30
    rows = (len(stills) + columns - 1) // columns
    sheet = Image.new('RGB', (columns * (cell_w + 10) + 10, rows * (cell_h + label + 10) + 10), (18, 18, 22))
    draw = ImageDraw.Draw(sheet)
    for i, (name, caption, shot, dim) in enumerate(stills):
        x, y = 10 + (i % columns) * (cell_w + 10), 10 + (i // columns) * (cell_h + label + 10)
        sheet.paste(Image.eval(shot, lambda v: int(v * dim)) if dim < 1.0 else shot, (x, y))
        draw.text((x, y + cell_h + 3), re.sub(r'^(\S+ )?(\d\d:\d\d)\s*', r'\2  ', caption)[:52], fill=(225, 225, 230), font=small)
    sheet.save(os.path.join(out, 'workday-contact-sheet.png'))

    with open(os.path.join(out, 'workday-timeline.md'), 'w') as file:
        file.write('# The simulated workday\n\nMonday 2026-01-05 07:55 to Tuesday 08:03, Asia/Dubai, on the simulated board.\n\n'
                   '| Simulated time | Moment | What happens | Backlight | Page |\n|---|---|---|---|---|\n')
        for when, name, caption, light, page in timeline:
            file.write('| %s | `%s` | %s | %s | %s |\n' % (when, name, re.sub(r'^(\S+ )?\d\d:\d\d\s*', '', caption), light, page))
        file.write('\nThe Buddy asked the laptop to: %s.\n' % (', '.join(laptop.asked) or 'nothing'))
        file.write('Link rounds without an answer: %d.\n' % laptop.unanswered)
    with open(os.path.join(out, 'workday-serial.log'), 'w') as file:
        file.write(log.replace('\r', ''))
    print('workday: %d moments, %d frames -> %s' % (len(stills), count[0], out))


if __name__ == '__main__':
    film(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'out', 'workday'))
