#!/usr/bin/env python3
"""Draw the blog figures (SVG plus PNG at 2x) and copy the chosen screenshots.

    python3 docs/blog/make_figures.py

Needs Pillow and rsvg-convert. Text is measured with the Inter and DejaVu Sans
Mono files installed here, so the layout holds where those fonts render the SVG.
"""
import base64
import functools
import html
import math
import os
import random
import subprocess

from PIL import Image, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(HERE, '..', 'guide', 'shots')
FIGURES = os.path.join(HERE, 'figures')
IMAGES = os.path.join(HERE, 'images')

# Surface, ink and the accent family. PURPLE and TEAL are the buddy's own; the
# *_MARK pair carries data in the graphs (dataviz validator: chroma, CVD and
# contrast pass on BG), so the purple is the Odoo hue with more chroma.
BG, CARD, LINE, GRID, AXIS = '#FBF9FA', '#FFFFFF', '#E6DDE3', '#ECE5EA', '#CFC4CC'
INK, INK2, MUTED = '#241A22', '#55464F', '#6F6069'
PURPLE, PURPLE_MARK, PURPLE_TIP, PURPLE_WASH = '#714B67', '#7A3A6C', '#CDB3C5', '#F4EDF2'
TEAL, TEAL_MARK, TEAL_TIP, TEAL_INK, TEAL_WASH = '#4AC3CB', '#1F9AA3', '#A9DCE0', '#017E84', '#E8F6F7'
GRAY_MARK, GRAY_TIP = '#BDB1BA', '#E0D8DE'
SCREEN, SCREEN_CARD, SCREEN_INK, SCREEN_INK2 = '#1C171E', '#2D2630', '#F7F3F7', '#CBC0C8'
SANS = "Inter, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif"
MONO = "'DejaVu Sans Mono', Menlo, Consolas, monospace"
FONT_FILES = {
    (400, False): '/usr/share/fonts/opentype/inter/Inter-Regular.otf',
    (600, False): '/usr/share/fonts/opentype/inter/Inter-SemiBold.otf',
    (700, False): '/usr/share/fonts/opentype/inter/Inter-Bold.otf',
    (400, True): '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',
    (700, True): '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf',
}


@functools.cache
def _font(size, weight, mono):
    return ImageFont.truetype(FONT_FILES[(weight, mono)], size)


def text_width(text, size=16, weight=400, mono=False):
    return _font(size, weight, mono).getlength(text)


def wrap(text, width, size=16, weight=400):
    lines, line = [], ''
    for word in text.split(' '):   # not split(): it would break a no-break space
        trial = f'{line} {word}'.strip()
        if line and text_width(trial, size, weight) > width:
            lines.append(line)
            line = word
        else:
            line = trial
    return lines + [line]


def esc(text):
    return html.escape(str(text), quote=True)


class Figure:
    def __init__(self, name, width, height, title, desc):
        self.name, self.w, self.h = name, width, height
        self.title, self.desc = title, desc
        self.parts, self.clips = [], 0

    def add(self, markup):
        self.parts.append(markup)

    def text(self, x, y, s, size=16, weight=400, fill=INK, anchor='start', mono=False, width=None, spacing=0, halo=None):
        if width is not None:   # a layout check, not a clip: fail loudly
            used = text_width(s, size, weight, mono) + spacing * len(s)
            assert used <= width, f'{self.name}: "{s}" is {used:.0f} px, room for {width:.0f}'
        family = f' font-family="{MONO}"' if mono else ''
        letter = f' letter-spacing="{spacing}"' if spacing else ''
        # halo: a ring of the background colour, so a label crossing a line stays readable
        ring = f' stroke="{halo}" stroke-width="5" stroke-linejoin="round" paint-order="stroke"' if halo else ''
        self.add(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" font-weight="{weight}" fill="{fill}" '
                 f'text-anchor="{anchor}"{family}{letter}{ring}>{esc(s)}</text>')

    def label(self, x, y, s, fill=MUTED, size=13, anchor='start'):
        """Small capitals heading over a group."""
        self.text(x, y, s.upper(), size, 600, fill, anchor, spacing=1.2)

    def rect(self, x, y, w, h, fill, rx=0, stroke=None, sw=1, extra=''):
        line = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ''
        self.add(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}" fill="{fill}"{line}{extra}/>')

    def line(self, x1, y1, x2, y2, stroke, sw=1):
        self.add(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{stroke}" '
                 f'stroke-width="{sw}" stroke-linecap="round"/>')

    def path(self, d, fill='none', stroke=None, sw=1, extra=''):
        line = f' stroke="{stroke}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"' if stroke else ''
        self.add(f'<path d="{d}" fill="{fill}"{line}{extra}/>')

    def circle(self, x, y, r, fill, stroke=None, sw=1):
        line = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ''
        self.add(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r}" fill="{fill}"{line}/>')

    def arrow(self, x1, y1, x2, y2, color=INK2, sw=1.6, head=9, both=False):
        """A straight line with a filled head at the end (and the start when both)."""
        angle = math.atan2(y2 - y1, x2 - x1)
        def tip(x, y, a):
            left = (x - head * math.cos(a - 0.42), y - head * math.sin(a - 0.42))
            right = (x - head * math.cos(a + 0.42), y - head * math.sin(a + 0.42))
            self.path(f'M{x:.1f},{y:.1f} L{left[0]:.1f},{left[1]:.1f} L{right[0]:.1f},{right[1]:.1f} Z', color)
        back = head * 0.8
        sx = x1 + (back * math.cos(angle) if both else 0)
        sy = y1 + (back * math.sin(angle) if both else 0)
        self.line(sx, sy, x2 - back * math.cos(angle), y2 - back * math.sin(angle), color, sw)
        tip(x2, y2, angle)
        if both:
            tip(x1, y1, angle + math.pi)

    def bar(self, x0, x1, y, h, fill, rounded_end=True, tooltip=None):
        """A bar growing right from x0: square at the baseline, 4 px round at the data end."""
        r = min(4, (x1 - x0) / 2, h / 2) if rounded_end else 0
        d = (f'M{x0:.1f},{y:.1f} H{x1 - r:.1f} Q{x1:.1f},{y:.1f} {x1:.1f},{y + r:.1f} '
             f'V{y + h - r:.1f} Q{x1:.1f},{y + h:.1f} {x1 - r:.1f},{y + h:.1f} H{x0:.1f} Z')
        self.add(f'<path d="{d}" fill="{fill}">' + (f'<title>{esc(tooltip)}</title>' if tooltip else '') + '</path>')

    def pill(self, x0, x1, y, h, fill, tooltip=None):
        """A floating range: no baseline, so both ends round."""
        self.add(f'<rect x="{x0:.1f}" y="{y:.1f}" width="{max(x1 - x0, h / 2):.1f}" height="{h}" rx="{min(4, h / 2)}" '
                 f'fill="{fill}">' + (f'<title>{esc(tooltip)}</title>' if tooltip else '') + '</rect>')

    def image(self, x, y, w, src, rx=6, frame=SCREEN):
        """A buddy screenshot, 320x172, inside a dark bezel."""
        h = w * 172 / 320
        with open(src, 'rb') as file:
            data = base64.b64encode(file.read()).decode()
        self.clips += 1
        self.rect(x - 3, y - 3, w + 6, h + 6, frame, rx + 2)
        self.add(f'<clipPath id="shot{self.clips}"><rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" rx="{rx}"/></clipPath>'
                 f'<image x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" clip-path="url(#shot{self.clips})" '
                 f'preserveAspectRatio="none" href="data:image/png;base64,{data}"/>')
        return h

    def header(self, title, subtitle, x=40):
        self.text(x, 54, title, 27, 700)
        if subtitle:
            self.text(x, 84, subtitle, 16, 400, INK2, width=self.w - 2 * x)

    def footer(self, s):
        self.text(40, self.h - 22, s, 13, 400, MUTED)

    def save(self):
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
               f'viewBox="0 0 {self.w} {self.h}" role="img" aria-labelledby="t d" font-family="{SANS}">'
               f'<title id="t">{esc(self.title)}</title><desc id="d">{esc(self.desc)}</desc>'
               f'<rect x="0.5" y="0.5" width="{self.w - 1}" height="{self.h - 1}" rx="14" fill="{BG}" stroke="{LINE}"/>'
               + '\n'.join(self.parts) + '</svg>\n')
        svg_path = os.path.join(FIGURES, f'{self.name}.svg')
        with open(svg_path, 'w') as file:
            file.write(svg)
        subprocess.run(['rsvg-convert', '-z', '2', '-o', os.path.join(FIGURES, f'{self.name}.png'), svg_path], check=True)


def step_circle(fig, x, y, n, fill=PURPLE):
    fig.circle(x, y, 12, fill)
    fig.text(x, y + 5, str(n), 14, 700, '#FFFFFF', 'middle')


def card(fig, x, y, w, h, title, detail=None, fill=CARD, stroke=LINE, ink=INK, ink2=INK2, rx=10):
    fig.rect(x, y, w, h, fill, rx, stroke)
    if detail:
        fig.text(x + 16, y + h / 2 - 4, title, 16, 600, ink, width=w - 32)
        fig.text(x + 16, y + h / 2 + 16, detail, 14, 400, ink2, width=w - 32)
    else:
        fig.text(x + 16, y + h / 2 + 6, title, 16, 600, ink, width=w - 32)


# ── Graphs ────────────────────────────────────────────────────────────────


def frame_rate():
    rows = [
        ('group', 'Normal pages: face, clock, weather, AbdA, animations'),
        ('row', 'Start: the whole frame sent every time', 11, 12, False),
        ('row', 'Wi-Fi upkeep every 500 ms, not every frame', 14, 14, False),
        ('row', 'Only changed rows sent, frames paced', 29, 30, False),
        ('row', 'Knock watch added, sensor bus at 100 kHz', 26, 28, False),
        ('row', 'Sensor bus at 400 kHz', 29, 29, True),
        ('row', 'Animation page, GIFs from the SD card', 21, 23, True),
        ('group', 'Bug Hop, the game'),
        ('row', 'Start', 13, 16, False),
        ('row', 'Its own partial flush: only the rows that move', 29, 40, True),
    ]
    width, x0, x1 = 900, 412, 812
    def X(fps):
        return x0 + fps * (x1 - x0) / 40
    top, pitch, bar_h = 168, 40, 20
    height = top + sum(36 if kind == 'group' else pitch for kind, *_ in rows) + 92
    desc = 'Frames per second on the board, lowest to highest reading. ' + '; '.join(
        f'{r[1]}: {r[2]}' + (f' to {r[3]}' if r[3] != r[2] else '') for r in rows if r[0] == 'row')
    fig = Figure('frame-rate', width, height, 'Frames per second, before and after', desc)
    fig.header('Frames per second, before and after', 'What each change bought, measured on the board')
    # legend
    lx = 40
    for fill, name in ((PURPLE_MARK, "today's firmware"), (GRAY_MARK, 'earlier builds')):
        fig.rect(lx, 112, 14, 14, fill, 3)
        fig.text(lx + 22, 124, name, 14, 400, INK2)
        lx += 30 + text_width(name, 14)
    fig.rect(lx, 112, 8, 14, GRAY_MARK, 2)
    fig.rect(lx + 10, 112, 18, 14, GRAY_TIP, 3)
    fig.text(lx + 36, 124, 'lighter tip: the spread of the readings', 14, 400, INK2)
    fig.line(40, 140, 40, 156, '#9A8A96', 1.2)
    fig.text(52, 153, 'the cap: frames are paced at one every 33 ms', 14, 400, INK2)

    y = top
    marks, spans = [], []   # spans: the y range of each group's rows, where the grid runs
    for kind, label, *values in rows:
        if kind == 'group':
            fig.text(40, y + 22, label, 15, 700, INK, width=width - 80)
            y += 36
            spans.append([y, y])
            continue
        lo, hi, now = values
        marks.append((y, label, lo, hi, now))
        y += pitch
        spans[-1][1] = y
    plot_bottom = y
    for fps in (0, 10, 20, 30, 40):
        for start, end in spans:
            fig.line(X(fps), start, X(fps), end, GRID if fps else AXIS, 1)
        fig.text(X(fps), plot_bottom + 22, str(fps), 14, 400, MUTED, 'middle')
    fig.text(x1, plot_bottom + 44, 'frames per second', 14, 400, MUTED, 'end')
    for start, end in spans:
        fig.line(X(30), start, X(30), end, '#9A8A96', 1.2)
    for y, label, lo, hi, now in marks:
        body, tip = (PURPLE_MARK, PURPLE_TIP) if now else (GRAY_MARK, GRAY_TIP)
        cy = y + (pitch - bar_h) / 2
        fig.text(x0 - 14, cy + 15, label, 15, 600 if now else 400, INK if now else INK2, 'end', width=x0 - 54)
        tooltip = f'{label}: {lo}' + (f'–{hi}' if hi != lo else '') + ' fps'
        fig.bar(x0, X(lo), cy, bar_h, body, rounded_end=hi == lo, tooltip=tooltip)
        if hi != lo:
            fig.bar(X(lo) + 2, X(hi), cy, bar_h, tip, tooltip=tooltip)
        fig.text(X(hi) + 8, cy + 15, f'{lo}–{hi}' if hi != lo else str(lo), 15, 600 if now else 400, INK, halo=BG)
    fig.footer('Measured on the real board, 18–19 November 2026. The game runs unpaced, so it can pass the cap.')
    fig.save()


def knock_strengths():
    rows = [
        ('At rest on the desk', None, 0.01, 0.05, '0.01–0.05', GRAY_MARK),
        ('Desk bumps, now and then', None, 0.10, 0.17, '0.10–0.17', GRAY_MARK),
        ('Desk knocks, first method', 'change of the total pull', 0.08, 0.19, '0.08–0.19', TEAL_MARK),
        ('Knocks on the buddy, final method', 'change in every direction', 0.27, 3.2, '0.27–3.2', PURPLE_MARK),
        ('Screen taps and handling', 'turned away by the touch, or by moving on', 1.1, 2.5, '1.1–2.5', GRAY_MARK),
    ]
    width, x0, x1 = 900, 372, 822
    lo_log, hi_log = -2, math.log10(5)
    def X(g):
        return x0 + (math.log10(g) - lo_log) / (hi_log - lo_log) * (x1 - x0)
    top, pitch, bar_h = 196, 60, 18
    height = top + len(rows) * pitch + 184
    desc = ('Jolts in g on a log scale. ' + '; '.join(f'{r[0]}: {r[4]} g' for r in rows)
            + '. A knock starts at 0.25 g; still means under 0.08 g; a second knock needs 25 % of the first.')
    fig = Figure('knock-strengths', width, height, 'How hard is a knock?', desc)
    fig.header('How hard is a knock?', 'Jolts measured on the board, in g, on a log scale')
    lx = 40
    for fill, name in ((PURPLE_MARK, 'knocks, final method'), (TEAL_MARK, 'knocks, first method'), (GRAY_MARK, 'not a knock')):
        fig.rect(lx, 112, 14, 14, fill, 3)
        fig.text(lx + 22, 124, name, 14, 400, INK2)
        lx += 34 + text_width(name, 14)
    plot_top, plot_bottom = top - 8, top + len(rows) * pitch
    fig.rect(X(0.25), plot_top, width - 40 - X(0.25), plot_bottom - plot_top, PURPLE_WASH)
    for g in (0.01, 0.03, 0.1, 0.3, 1, 3):
        fig.line(X(g), plot_top, X(g), plot_bottom, GRID, 1)
        fig.text(X(g), plot_bottom + 22, f'{g:g}', 14, 400, MUTED, 'middle')
    fig.text(x1, plot_bottom + 44, 'jolt, g (log scale)', 14, 400, MUTED, 'end')
    for g, name, anchor, dx in ((0.08, 'still: under 0.08 g', 'end', -6), (0.25, 'a knock starts at 0.25 g', 'start', 6)):
        fig.line(X(g), plot_top - 30, X(g), plot_bottom, '#9A8A96', 1.2)
        fig.text(X(g) + dx, plot_top - 18, name, 13, 600, INK2, anchor)
    for i, (name, detail, lo, hi, value, fill) in enumerate(rows):
        y = top + i * pitch + (pitch - bar_h) / 2
        if detail:
            fig.text(x0 - 14, y + 7, name, 15, 600, INK, 'end', width=x0 - 54)
            fig.text(x0 - 14, y + 26, detail, 13, 400, INK2, 'end', width=x0 - 54)
        else:
            fig.text(x0 - 14, y + 14, name, 15, 600, INK, 'end', width=x0 - 54)
        fig.pill(X(lo), X(hi), y, bar_h, fill, f'{name}: {value} g')
        fig.text(X(hi) + 8, y + 14, value, 14, 400, INK, halo=PURPLE_WASH if lo > 0.25 else BG)
    notes = [
        (TEAL_MARK, 'First method: desk knocks overlap the desk bumps and sit just above rest. No threshold splits them.'),
        (PURPLE_MARK, 'Final method: every knock clears 0.25 g while rest stays under 0.08 g.'),
        (GRAY_MARK, 'A second knock must reach 25 % of the first; weaker jolts are the buddy rocking on its stand.'),
    ]
    for i, (fill, note) in enumerate(notes):
        ny = plot_bottom + 78 + i * 24
        fig.rect(40, ny - 11, 10, 10, fill, 2)
        fig.text(58, ny - 1, note, 14, 400, INK2, width=width - 98)
    fig.footer('Measured on the real board, 18–19 November 2026.')
    fig.save()


def link_quality():
    width, x0, x1 = 900, 330, 800
    desc = ('Wi-Fi link, 40 sealed pings per test. Pings lost: on the desk 0 of 40; weak spot on a charger 8 of 40; '
            'same spot, one run 13 of 20. Round trip: desk median 4 to 60 ms; weak spot median 187 ms, slowest 762 ms. '
            'Firmware updates in the weak spot broke off at 67 to 135 KB of 1.9 MB.')
    fig = Figure('link-quality', width, 700, 'The Wi-Fi link, on the desk and in a weak spot', desc)
    fig.header('The Wi-Fi link, on the desk and in a weak spot', '40 sealed pings per test')
    lx = 40
    for fill, name in ((TEAL_MARK, 'on the desk'), (PURPLE_MARK, 'weak spot, on a charger')):
        fig.rect(lx, 112, 14, 14, fill, 3)
        fig.text(lx + 22, 124, name, 14, 400, INK2)
        lx += 34 + text_width(name, 14)

    def panel(y, title, rows, maximum, ticks, unit):
        fig.text(40, y, title, 16, 700)
        top = y + 16
        bottom = top + len(rows) * 38
        def X(v):
            return x0 + v * (x1 - x0) / maximum
        for t in ticks:
            fig.line(X(t), top, X(t), bottom, GRID if t else AXIS, 1)
            fig.text(X(t), bottom + 20, f'{t}{unit}', 13, 400, MUTED, 'middle')
        for i, (name, lo, hi, fill, tip, value) in enumerate(rows):
            by = top + i * 38 + 9
            fig.text(x0 - 14, by + 15, name, 15, 400, INK, 'end', width=x0 - 54)
            if hi > 0:
                fig.bar(x0, X(lo), by, 20, fill, rounded_end=hi == lo, tooltip=f'{name}: {value}')
                if hi != lo:
                    fig.bar(X(lo) + 2, X(hi), by, 20, tip, tooltip=f'{name}: {value}')
            fig.text(X(hi) + 8, by + 15, value, 15, 600, INK, halo=BG)
        return bottom + 52

    y = panel(170, 'Pings lost', [
        ('On the desk', 0, 0, TEAL_MARK, TEAL_TIP, '0 of 40'),
        ('Weak spot', 20, 20, PURPLE_MARK, PURPLE_TIP, '8 of 40'),
        ('Same spot, worst run', 65, 65, PURPLE_MARK, PURPLE_TIP, '13 of 20'),
    ], 100, (0, 25, 50, 75, 100), ' %')
    y = panel(y + 8, 'Round trip', [
        ('On the desk, median', 4, 60, TEAL_MARK, TEAL_TIP, '4–60 ms'),
        ('Weak spot, median', 187, 187, PURPLE_MARK, PURPLE_TIP, '187 ms'),
        ('Weak spot, slowest', 762, 762, PURPLE_MARK, PURPLE_TIP, '762 ms'),
    ], 800, (0, 200, 400, 600, 800), ' ms')
    # where the old, unresumable updates broke off
    fig.text(40, y + 8, 'Where firmware updates broke off in the weak spot', 16, 700)
    ty = y + 54
    def K(kb):
        return x0 + kb * (x1 - x0) / 1900
    fig.text(x0 - 14, ty + 15, 'The 1.9 MB image', 15, 400, INK, 'end')
    fig.rect(x0, ty + 4, x1 - x0, 12, GRID, 6)
    fig.pill(K(67), K(135), ty + 1, 18, PURPLE_MARK, 'broke off between 67 and 135 KB')
    fig.text(K(67), ty - 10, 'broke off at 67–135 KB, three times', 15, 600, INK)
    fig.text(x0, ty + 40, '0', 13, 400, MUTED, 'middle')
    fig.text(x1, ty + 40, '1.9 MB', 13, 400, MUTED, 'middle')
    fig.footer('Measured on the real board, 18–19 November 2026. Each break cost the whole transfer until updates could resume.')
    fig.save()


def update_time():
    width, x0, x1 = 900, 300, 800
    desc = ('Installing a 1.9 MB firmware: about 23 to 25 s over USB, about 90 s over Wi-Fi in 1 KB pieces. '
            'Resume test: the transfer was cut at 63 % and the same command went on at 65 % and finished.')
    fig = Figure('update-time', width, 484, 'Installing a new firmware', desc)
    fig.header('Installing a new firmware', 'A 1.9 MB image, over the cable and over the air')
    fig.text(40, 140, 'Time to install', 16, 700)
    top = 156
    def X(s):
        return x0 + s * (x1 - x0) / 100
    for t in (0, 25, 50, 75, 100):
        fig.line(X(t), top, X(t), top + 76, GRID if t else AXIS, 1)
        fig.text(X(t), top + 96, f'{t} s', 13, 400, MUTED, 'middle')
    for i, (name, lo, hi, value) in enumerate((('USB cable', 23, 25, '23–25 s'),
                                               ('Wi-Fi, signed, 1 KB pieces', 90, 90, 'about 90 s'))):
        by = top + 9 + i * 38
        fig.text(x0 - 14, by + 15, name, 15, 400, INK, 'end', width=x0 - 54)
        fig.bar(x0, X(lo), by, 20, PURPLE_MARK, rounded_end=hi == lo, tooltip=f'{name}: {value}')
        if hi != lo:
            fig.bar(X(lo) + 2, X(hi), by, 20, PURPLE_TIP, tooltip=f'{name}: {value}')
        fig.text(X(hi) + 8, by + 15, value, 15, 600, INK)

    fig.text(40, 306, 'A broken-off update goes on where it stopped', 16, 700)
    ty = 336
    def P(pct):
        return x0 + pct * (x1 - x0) / 100
    fig.text(x0 - 14, ty + 15, 'First run', 15, 400, INK, 'end')
    fig.bar(x0, P(63), ty, 20, PURPLE_MARK, tooltip='first run: 0 to 63 %')
    fig.text(P(63) + 8, ty + 15, 'Wi-Fi cut at 63 %', 15, 600, INK)
    fig.text(x0 - 14, ty + 53, 'Same command again', 15, 400, INK, 'end')
    fig.rect(x0, ty + 38, P(65) - x0 - 2, 20, GRAY_TIP, 4)
    fig.bar(P(65), P(100), ty + 38, 20, PURPLE_MARK, tooltip='second run: 65 to 100 %')
    fig.text(P(65) - 10, ty + 53, 'kept from the first run · goes on at 65 %', 14, 600, INK, 'end')
    fig.text(P(100), ty + 82, '100 %', 13, 400, MUTED, 'middle')
    fig.text(x0, ty + 82, '0 %', 13, 400, MUTED, 'middle')
    fig.footer('Measured on the real board, 18–19 November 2026. The buddy keeps a broken-off update for 10 minutes.')
    fig.save()


# ── Diagrams ──────────────────────────────────────────────────────────────


def architecture():
    width, height = 960, 900
    desc = ('The laptop reads www.odoo.com through a Chrome extension, GitHub through gh, Google Calendar through '
            "GNOME's calendar cache, plus Claude Code hooks, the screen lock and the microphone. The buddy-link "
            'service sends it all to the buddy over Wi-Fi, every packet sealed with AES-256-GCM, with USB as the '
            'fallback. The buddy fetches weather, its GitHub profile and the time itself.')
    fig = Figure('architecture', width, height, 'How the pieces talk', desc)
    fig.header('How the pieces talk', 'The laptop tells the buddy what it knows. The public bits, the buddy fetches itself.')
    cols, cw = (52, 344, 636), 272
    fig.label(52, 128, 'Internet, reached through the laptop', TEAL_INK)
    iy = 140
    for x, (title, detail) in zip(cols, (('www.odoo.com', 'hours, tasks, meetings'),
                                         ('GitHub', 'pull requests and checks'),
                                         ('Google Calendar', 'synced to the laptop by GNOME'))):
        card(fig, x, iy, cw, 58, title, detail, fill=TEAL_WASH, stroke='#BFE2E5')

    ly = 234
    fig.rect(32, ly, 896, 344, PURPLE_WASH, 16, '#E2D4DE')
    fig.label(52, ly + 326, 'Laptop · Ubuntu', PURPLE)
    ay = ly + 46
    fig.rect(44, ay - 10, 872, 160, 'none', 12, '#D9C8D4')
    for x in cols:
        fig.arrow(x + cw / 2, iy + 58, x + cw / 2, ay, TEAL_INK)
    sources = (('Chrome extension', 'reads Odoo with my session'),
               ('gh', 'PRs, reviews, CI, every 10 min'),
               ('GNOME calendar cache', 'read-only, no Google keys'),
               ('Claude Code hooks', 'waiting for me, done, usage'),
               ('Screen lock', 'logind: locked or not'),
               ('Microphone', 'PipeWire: is something recording'))
    for i, (title, detail) in enumerate(sources):
        card(fig, cols[i % 3], ay + (i // 3) * 74, cw, 62, title, detail)
    hub_y = ay + 194
    fig.arrow(480, ay + 150, 480, hub_y, PURPLE, 2)
    fig.text(494, ay + 177, 'all six, every 5 seconds', 14, 600, PURPLE)
    fig.rect(300, hub_y, 360, 60, PURPLE, 10)
    fig.text(480, hub_y + 26, 'buddy-link', 17, 700, '#FFFFFF', 'middle')
    fig.text(480, hub_y + 46, 'a small Python service', 14, 400, '#EADFE6', 'middle')

    by = 662
    fig.arrow(440, hub_y + 60, 440, by, PURPLE, 3.5, 12, both=True)
    fig.arrow(380, hub_y + 60, 380, by, '#9A8A96', 1.6, 9, both=True)
    fig.text(460, 604, 'Wi-Fi, UDP: every packet sealed with AES-256-GCM', 15, 700, INK)
    fig.text(460, 624, 'the buddy only answers; it never calls the laptop', 14, 400, INK2)
    fig.text(460, 644, 'buddy-say, signed updates and animations use it too', 14, 400, INK2)
    fig.text(362, 624, 'USB: fallback, settings, rescue', 14, 600, INK2, 'end')

    fig.rect(32, by, 568, 208, SCREEN, 16)
    fig.text(52, by + 32, 'Monitor Buddy', 18, 700, '#FFFFFF')
    fig.text(52, by + 54, 'Waveshare ESP32-C6-Touch-LCD-1.47 · Wi-Fi · 8 MB flash', 14, 400, SCREEN_INK2)
    chips = (('Touch screen', '1.47 in, 320 × 172'), ('Motion sensor', 'knocks, shakes, lying flat'),
             ('microSD card', 'the animations'), ('Two firmware slots', 'a bad update rolls back'))
    for i, (title, detail) in enumerate(chips):
        card(fig, 52 + (i % 2) * 272, by + 72 + (i // 2) * 64, 256, 54, title, detail,
             fill=SCREEN_CARD, stroke=None, ink='#FFFFFF', ink2=SCREEN_INK2, rx=8)

    fig.label(640, by + 10, 'Internet, reached by the buddy', TEAL_INK)
    for i, (title, detail) in enumerate((('Open-Meteo', 'weather, every 15 min'),
                                         ('GitHub profile', 'streak and stars, every 30 min'),
                                         ('NTP', 'the time'))):
        cy = by + 24 + i * 62
        card(fig, 640, cy, 288, 52, title, detail, fill=TEAL_WASH, stroke='#BFE2E5')
        fig.arrow(600, cy + 26, 640, cy + 26, TEAL_INK)
    fig.save()


def update_flow():
    width, left, right = 900, 220, 660
    desc = ('Sequence of a signed firmware update over Wi-Fi. The laptop signs the image with ECDSA P-256; the key '
            'never leaves the laptop. It sends B with the size and signature; the buddy answers the offset to start '
            'from. 1 KB D pieces follow; the buddy writes the spare slot and hashes as it goes, then checks the '
            'signature against the public key built into its firmware, refusing a mismatch. It restarts into the new '
            'slot on trial; the first link packet keeps it, a crash before that rolls back. A broken-off update '
            'goes on where it stopped: tested cut at 63 %, resumed at 65 %.')
    fig = Figure('update-flow', width, 1000, 'A signed firmware update over Wi-Fi', desc)
    fig.header('A signed firmware update over Wi-Fi', 'The private key stays on the laptop. The buddy can only check.')

    def head(x, title, detail, fill):
        fig.rect(x - 130, 112, 260, 56, fill, 10)
        fig.text(x, 137, title, 16, 700, '#FFFFFF', 'middle')
        fig.text(x, 157, detail, 13, 400, '#EADFE6', 'middle', mono=True)
    head(left, 'Laptop', 'buddy_flash.py', PURPLE)
    head(right, 'Monitor Buddy', 'the firmware', SCREEN)

    def box(x, y, n, title, details, fill=CARD, stroke=LINE, ink=INK):
        h = 40 + 20 * len(details)
        fig.rect(x - 175, y, 350, h, fill, 10, stroke)
        if n:
            step_circle(fig, x - 151, y + 22, n)
        tx = x - 129 if n else x - 159
        fig.text(tx, y + 27, title, 16, 700, ink, width=x + 160 - tx)
        for i, line in enumerate(details):
            fig.text(tx, y + 49 + 20 * i, line, 14, 400, INK2, width=x + 160 - tx)
        return y + h

    def message(y, text, to_right, n=None, weight=600):
        x1, x2 = (left, right) if to_right else (right, left)
        fig.arrow(x1, y, x2, y, PURPLE if to_right else INK2, 2 if to_right else 1.6)
        mid = (left + right) / 2
        if n:
            tw_ = text_width(text, 15, weight)
            step_circle(fig, mid - tw_ / 2 - 6, y - 15, n)
            fig.text(mid - tw_ / 2 + 14, y - 10, text, 15, weight, INK, halo=BG)
        else:
            fig.text(mid, y - 10, text, 14, 400, INK2, 'middle', halo=BG)

    def refusal(y, x, text):
        lines = wrap(text, 252, 14)
        h = 18 + 20 * len(lines)
        fig.rect(x - 145, y, 320, h, BG, 10, '#B9ABB5')
        fig.circle(x - 123, y + h / 2, 10, '#8A7A86')
        fig.path(f'M{x - 127},{y + h / 2 - 4} l8,8 m0,-8 l-8,8', stroke='#FFFFFF', sw=2)
        for i, line in enumerate(lines):
            fig.text(x - 103, y + 25 + 20 * i, line, 14, 400, INK2, width=260)
        return y + h

    lifeline_top = 168
    y = 192
    y = box(left, y, 1, 'Sign the image', ['ECDSA P-256 over SHA-256 (RFC 6979)', 'the key never leaves the laptop'])
    message(y + 42, 'B: size and signature', True, 2)
    message(y + 86, 'the offset to start from: 0, or where it stopped', False)
    y += 140
    for k in range(3):
        fig.arrow(left, y + 7 * k, right, y + 7 * k, PURPLE, 1.4, 7)
    tw3 = text_width('D: 1 KB pieces, about 1,900 of them', 15, 600)
    step_circle(fig, (left + right) / 2 - tw3 / 2 - 6, y - 15, 3)
    fig.text((left + right) / 2 - tw3 / 2 + 14, y - 10, 'D: 1 KB pieces, about 1,900 of them', 15, 600, INK, halo=BG)
    y = box(right, y + 34, None, 'Writes the spare slot', ['and hashes every piece as it comes'])
    y = box(right, y + 18, 4, 'Checks the signature', ['against UPDATE_KEY, the public half', 'built into the running firmware'])
    y = refusal(y + 12, right, 'No match: it answers "refused" and drops the update.')
    message(y + 40, 'ok: restarting', False)
    y = box(right, y + 60, 5, 'Restarts into the new slot', ['on trial until it hears the laptop again'])
    message(y + 42, 'the next packet from buddy-link', True, 6)
    y = box(right, y + 62, None, 'Keeps the new firmware for good', [])
    y = refusal(y + 12, right, 'A crash or restart before that packet: back to the old firmware.')
    message(y + 40, 'health: the build id that runs', False)
    y = box(left, y + 60, 7, '"The Buddy runs the new firmware."', [])
    lifeline_bottom = y + 16
    for x in (left, right):
        fig.add(f'<line x1="{x}" y1="{lifeline_top}" x2="{x}" y2="{lifeline_bottom}" stroke="{AXIS}" stroke-width="2"/>')
    fig.parts.insert(0, fig.parts.pop())   # lifelines under everything
    fig.parts.insert(0, fig.parts.pop())

    py = lifeline_bottom + 28
    fig.rect(40, py, width - 80, 132, TEAL_WASH, 12, '#BFE2E5')
    fig.text(64, py + 32, 'Broken off halfway? Run the same command again.', 16, 700, INK)
    note = ('The buddy keeps a broken-off update for 10 minutes. Signing is deterministic, so the second run '
            'sends the very same B, and the buddy answers with the offset where it stopped.')
    for i, line in enumerate(wrap(note, width - 128, 14)):
        fig.text(64, py + 56 + 20 * i, line, 14, 400, INK2)
    track_y, x0, x1 = py + 100, 64, width - 64
    def P(pct):
        return x0 + (x1 - x0) * pct / 100
    fig.rect(x0, track_y, x1 - x0, 10, '#CFEAEC', 5)
    fig.bar(x0, P(63), track_y, 10, PURPLE_MARK)
    fig.bar(P(65), P(100), track_y, 10, PURPLE_MARK)
    fig.text(P(63), track_y - 6, 'cut at 63 %', 13, 600, INK, 'end', halo=TEAL_WASH)
    fig.text(P(65), track_y - 6, 'went on at 65 %', 13, 600, INK, halo=TEAL_WASH)
    fig.h = py + 132 + 30
    fig.save()


def face_priority():
    rows = [
        ('Knock greeting', "two knocks on its head: WHAT'S\u00a0UP?", 'mood_2_surprised.png'),
        ('Shake', 'dizzy while shaken, then a flat REALLY?', 'day_dizzy_0.png'),
        ('Night', '23:00 to 07:00: heavy eyes, a dim screen', 'status_night_0.png'),
        ('On air', 'something records from the microphone', 'day_onair.png'),
        ('Meeting soon', 'from 5 minutes before, Odoo or Google', 'day_meeting_name.png'),
        ('Focus timer', 'lying flat: it counts up, notifications off', 'day_timer.png'),
        ('Timer done', 'for 15 seconds after standing it up', 'day_timer_done.png'),
        ('Laptop locked', 'LUNCH BREAK from 12 to 14, else AWAY', 'status_lunch_0.png'),
        ('Claude needs you', 'a Claude Code session waits for me', 'status_claude_needs_0.png'),
        ('Log your hours', 'the day before has under 4 hours logged', 'day_nudge_1.png'),
        ('CI red', 'a recent pull request fails its checks', 'status_ci_1.png'),
        ('Claude done', 'a Claude Code session just finished', 'status_claude_done_0.png'),
        ('Locked in', 'an Odoo task is in development', 'status_lockedin_0.png'),
        (None, 'Nothing applies: the plain face. A tap steps through nine moods.', 'mood_0_normal.png'),
    ]
    width, pitch, top = 900, 86, 128
    desc = 'Face statuses from highest to lowest priority: ' + '; '.join(
        f'{i + 1}. {r[0]} ({r[1]})' for i, r in enumerate(rows) if r[0]) + '. Otherwise the plain face.'
    fig = Figure('face-priority', width, top + 7 * pitch + 24, 'Which face wins', desc)
    fig.header('Which face wins', 'Every frame, the firmware shows the first status that applies, highest first.')
    for col in range(2):
        cx = 40 + col * 440
        first, last = top + 30, top + 6 * pitch + 30
        fig.line(cx + 14, first, cx + 14, last, LINE, 2)
        for i in range(7):
            n = col * 7 + i
            title, detail, shot = rows[n]
            y = top + i * pitch
            if title:
                step_circle(fig, cx + 14, y + 30, n + 1)
            else:
                fig.circle(cx + 14, y + 30, 12, '#B9ABB5')
                fig.text(cx + 14, y + 35, '–', 14, 700, '#FFFFFF', 'middle')
            fig.image(cx + 40, y, 112, os.path.join(SHOTS, shot))
            tx = cx + 170
            if title:
                fig.text(tx, y + 22, title, 16, 700, INK, width=410 - 170)
                lines = wrap(detail, 236, 14)
                for k, line in enumerate(lines):
                    fig.text(tx, y + 43 + 18 * k, line, 14, 400, INK2, width=240)
            else:
                for k, line in enumerate(wrap(detail, 236, 14)):
                    fig.text(tx, y + 22 + 18 * k, line, 14, 400, INK2, width=240)
    fig.save()


def day_timeline():
    rows = [
        ('00:00', 'NIGHT', 'until 07:00: heavy eyes, the screen dimmed'),
        ('08:30', 'YESTERDAY', "first unlock of the day: yesterday's hours, tasks and pull requests, 15 s"),
        ('', 'GOOD MORNING', "then today's page: next meeting, hours, reviews, Claude's limits, 20 s"),
        ('08:31', "LOG MONDAY'S HOURS", 'Monday has under 4 hours logged: a tear runs until I log them'),
        ('09:10', 'LOCKED IN', 'a task of mine is in development'),
        ('10:55', 'MEETING IN 5 MIN', "counting down, taking turns with the meeting's name"),
        ('11:00', 'ON AIR', 'IN A MEETING m:ss, while something records from the microphone'),
        ('11:45', 'LOCKED IN', 'back to work'),
        ('12:30', 'LUNCH BREAK', 'the laptop is locked between 12:00 and 14:00'),
        ('13:15', 'LOCKED IN', ''),
        ('14:00', 'FOCUS 0:00', 'laid flat: the timer counts up, notifications go quiet'),
        ('15:30', 'FOCUSED 1:30:00', 'stood up: 15 s of pride, then fireworks (25 minutes or more)'),
        ('17:40', 'DAY DONE', "locked after 17:00: the day's recap for 30 s, then AWAY"),
        ('18:10', 'BUDDY', 'away for 30 minutes: the Buddy logo bounces like an idle DVD logo'),
        ('23:00', 'NIGHT', ''),
    ]
    width, top = 900, 130
    detail_x, detail_w = 352, 508
    heights = [52 if len(wrap(d, detail_w, 15)) < 2 else 68 for _, _, d in rows]
    desc = 'An example working day: ' + '; '.join(f'{t or "then"} {c}' + (f' ({d})' if d else '') for t, c, d in rows)
    fig = Figure('day-timeline', width, top + sum(heights) + 30, 'A working day in faces', desc)
    fig.header('A working day in faces', 'An example Tuesday. The times are made up; the rules are the firmware’s.')
    spine_x = 122
    y = top
    centers = []
    for (time, caption, detail), h in zip(rows, heights):
        cy = y + 20
        centers.append(cy)
        if time:
            fig.text(spine_x - 22, cy + 6, time, 16, 700, INK, 'end')
        chip_w = text_width(caption, 14, 700, mono=True) + 26
        fig.rect(150, cy - 16, chip_w, 32, SCREEN, 8)
        fig.text(163, cy + 5, caption, 14, 700, SCREEN_INK, mono=True)
        for k, line in enumerate(wrap(detail, detail_w, 15) if detail else []):
            fig.text(detail_x, cy + 5 + 19 * k, line, 15, 400, INK2)
        y += h
    fig.line(spine_x, centers[0], spine_x, centers[-1], AXIS, 2)
    for (time, caption, _), cy in zip(rows, centers):
        fill = '#3B2F39' if caption == 'NIGHT' else (PURPLE if time else TEAL_MARK)
        fig.circle(spine_x, cy, 6, fill, BG, 2)
    fig.save()


def knock_detector():
    width = 900
    desc = ('How a double knock is told apart, as an illustrative trace of the jolt over time. A knock is a jolt of '
            'at least 0.25 g after the buddy was still (under 0.08 g); the first needs 150 ms of stillness. The '
            'second comes 80 to 700 ms after the first; weaker jolts under 25 % of the first are the buddy rocking '
            'and are ignored. 250 ms with no third knock make it a double knock. A screen tap comes with a touch, '
            'picking the buddy up keeps it moving, and a third strong jolt cancels the pair.')
    fig = Figure('knock-detector', width, 776, 'Telling a double knock from everything else', desc)
    fig.header('Telling a double knock from everything else',
               'The motion sensor is read every 3 ms. A jolt is the change in acceleration, in every direction.')
    x0, x1, t_max = 90, 860, 1150
    base, g_px = 410, 175
    def X(t):
        return x0 + t * (x1 - x0) / t_max
    def Y(g):
        return base - g * g_px

    def trace(knocks, t_end, Xf, Yf, seed, noise=0.012, wobble=None):
        rng = random.Random(seed)
        points = []
        for t in range(0, t_end + 1, 3):
            v = noise + noise * rng.random()
            if wobble:
                v += wobble(t, rng)
            for t0, amp, tau in knocks:
                if t >= t0:
                    v += amp * math.exp(-(t - t0) / tau) * abs(math.cos(2 * math.pi * (t - t0) / 36))
            points.append(f'{Xf(t):.1f},{Yf(v):.1f}')
        fig.path('M' + ' L'.join(points), stroke=PURPLE_MARK, sw=2)

    fig.text(40, 128, 'A double knock, step by step (an illustrative trace)', 16, 700)
    for g, name in ((0.25, 'a knock: 0.25 g or more'), (0.08, 'still: under 0.08 g')):
        fig.line(x0, Y(g), x1, Y(g), PURPLE if g > 0.1 else TEAL_INK, 1.2)
        fig.text(x0 + 4, Y(g) - 6, name, 13, 600, PURPLE if g > 0.1 else TEAL_INK, halo=BG)
    fig.line(x0, base, x1, base, AXIS, 1)
    for t in range(0, t_max + 1, 200):
        fig.text(X(t), base + 22, f'{t} ms', 13, 400, MUTED, 'middle')
    trace(((250, 1.1, 22), (351, 0.26, 18), (700, 0.78, 20), (801, 0.12, 16)), t_max, X, Y, 4)

    def bracket(t1, t2, y, text, anchor='middle'):
        fig.path(f'M{X(t1):.1f},{y + 6} V{y} H{X(t2):.1f} V{y + 6}', stroke=INK2, sw=1.4)
        tx = {'middle': (X(t1) + X(t2)) / 2, 'start': X(t1), 'end': X(t2)}[anchor]
        fig.text(tx, y - 8, text, 14, 600, INK, anchor, halo=BG)
    bracket(100, 250, Y(0.45), 'still for 150 ms')
    bracket(250, 700, Y(1.24), 'the second knock: 80–700 ms later')
    bracket(700, 950, Y(0.45), '250 ms, no third knock')
    fig.text(X(250) + 10, Y(1.1) + 6, 'knock 1: 1.1 g', 14, 700, INK, halo=BG)
    fig.text(X(700) + 10, Y(0.78) + 6, 'knock 2: 0.78 g', 14, 700, INK, halo=BG)
    fig.line(X(351) + 3, Y(0.27), X(372), Y(0.58), INK2, 1)
    fig.text(X(372) + 4, Y(0.58) - 16, 'the buddy rocking on its stand:', 13, 400, INK2, halo=BG, width=X(690) - X(372))
    fig.text(X(372) + 4, Y(0.58), 'under 25 % of knock 1, ignored', 13, 400, INK2, halo=BG, width=X(690) - X(372))
    fig.line(X(950), Y(0.0), X(950), Y(0.92), PURPLE, 2)
    fig.circle(X(950), Y(0.92), 6, PURPLE)
    fig.text(X(950), Y(0.92) - 14, "double knock: WHAT'S UP?", 15, 700, PURPLE, 'middle', halo=BG)

    fig.text(40, 488, 'Not a knock', 16, 700)
    panels = (
        ('A tap on the screen', ['A strong jolt, but the touch', 'panel saw a finger.'],
         ((250, 1.8, 20),), None, (205, 320)),
        ('Picked up', ['It keeps moving and is', 'never still, so no knock starts.'],
         (), lambda t, rng: 0.45 + 0.35 * math.sin(t / 37) + 0.3 * math.sin(t / 13 + 1) + 0.25 * rng.random(), None),
        ('Typing, or knocking on', ['A third strong jolt', 'cancels the pair.'],
         ((120, 0.85, 18), (300, 0.7, 18), (470, 0.78, 18)), None, None),
    )
    for i, (title, lines, knocks, wobble, touch) in enumerate(panels):
        px = 40 + i * 284
        fig.rect(px, 504, 252, 242, CARD, 12, LINE)
        fig.circle(px + 26, 532, 10, '#8A7A86')
        fig.path(f'M{px + 22},{528} l8,8 m0,-8 l-8,8', stroke='#FFFFFF', sw=2)
        fig.text(px + 44, 537, title, 15, 700, INK, width=196)
        mx0, mx1, mbase = px + 16, px + 236, 650
        def MX(t, mx0=mx0, mx1=mx1):
            return mx0 + t * (mx1 - mx0) / 600
        def MY(g, mbase=mbase):
            return mbase - g * 46
        fig.line(mx0, MY(0.25), mx1, MY(0.25), PURPLE, 1)
        fig.line(mx0, mbase, mx1, mbase, AXIS, 1)
        trace(knocks, 600, MX, MY, 10 + i, wobble=wobble)
        if touch:
            fig.rect(MX(touch[0]), mbase + 6, MX(touch[1]) - MX(touch[0]), 8, TEAL_MARK, 4)
            fig.text(MX(touch[0]), mbase + 30, 'finger on the screen', 12, 600, TEAL_INK)
        for k, line in enumerate(lines):
            fig.text(px + 16, 704 + 19 * k, line, 14, 400, INK2, width=222)
    fig.save()


# ── Screenshots ───────────────────────────────────────────────────────────

# (source in docs/guide/shots, name in images/, boxes to blur at 1x: x0, y0, x1, y1)
SCREENSHOTS = (
    ('status_lockedin_0.png', 'face-locked-in.png', ()),
    ('status_lunch_0.png', 'face-lunch-break.png', ()),
    ('day_dizzy_0.png', 'face-dizzy.png', ()),
    ('day_really.png', 'face-really.png', ()),
    ('day_onair.png', 'face-on-air.png', ()),
    ('status_claude_needs_0.png', 'face-claude-needs-you.png', ()),
    ('day_nudge_1.png', 'face-log-hours.png', ()),
    ('day_today.png', 'page-today.png', ((82, 22, 204, 46),)),                        # the meeting's name
    ('page_odoo.png', 'page-odoo.png', ((16, 100, 304, 142), (204, 146, 276, 166))),  # task name and number
    ('gif_loading.png', 'anim-downloading-coffee.png', ()),
)


def copy_screenshots():
    """3x with hard pixel edges, so blogs that smooth images keep them crisp."""
    for source, target, boxes in SCREENSHOTS:
        shot = Image.open(os.path.join(SHOTS, source)).convert('RGB')
        for box in boxes:   # mosaic first: a blur alone can leave text guessable
            region = shot.crop(box)
            w, h = region.size
            mosaic = region.resize((max(1, w // 10), max(1, h // 10)), Image.BOX).resize((w, h), Image.NEAREST)
            shot.paste(mosaic.filter(ImageFilter.GaussianBlur(3)), box)
        shot.resize((shot.width * 3, shot.height * 3), Image.NEAREST).save(os.path.join(IMAGES, target), optimize=True)


if __name__ == '__main__':
    os.makedirs(FIGURES, exist_ok=True)
    os.makedirs(IMAGES, exist_ok=True)
    frame_rate()
    knock_strengths()
    link_quality()
    update_time()
    architecture()
    update_flow()
    face_priority()
    day_timeline()
    knock_detector()
    copy_screenshots()
