#!/usr/bin/env python3
"""Collect the pictures of the personal touches at the screen's size, 320x172.

    python docs/guide/persona_shots.py

Nine are screenshots from a real board (docs/images/board/classic_*.png, taken
over USB with `@MB SHOT` while `@MB STATE {"demo": N}` showed each one, with the
example names Sam and Pixel). The tenth, the bounce screensaver, is drawn here
the way drawBounce() in src/main.cpp lays it out, with the firmware's font: it
only comes after 30 minutes away. It writes docs/guide/shots/persona_*.png, the
sheet docs/images/personal.png, the four-style sheet docs/images/faces.png and
the full sheets docs/images/faces/faces_STYLE.png. Needs Pillow and, for the
bounce, the font header PlatformIO fetches with the build.
"""
import glob
import os
import re

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..', '..')
SHOTS = os.path.join(HERE, 'shots')
BOARD = os.path.join(ROOT, 'docs', 'images', 'board')
CAPTION = (148, 161, 172)   # rgb(150, 160, 175) as the 16-bit screen shows it
TEAL = (0, 198, 197)        # the bounce's first colour, rounded the same way

# (file, board screenshot): docs/images/board/classic_NAME.png, taken with the example names Sam and Pixel.
FRAMES = [('greeting', 'greeting'), ('welcome', 'welcome_back'), ('birthday', 'birthday'), ('anniversary', 'anniversary'),
          ('stretch', 'stretch'), ('lunch', 'lunch_time'), ('water', 'water'), ('message', 'message'), ('weekend', 'weekend')]


def font():
    """The 5 column bytes of each character, from the firmware's own font header."""
    paths = glob.glob(os.path.join(ROOT, '.pio', 'libdeps', '*', 'GFX Library for Arduino', 'src', 'font', 'glcdfont.h'))
    if not paths:
        raise SystemExit('Build the firmware once first (pio run): the font header comes with its libraries.')
    with open(paths[0]) as file:
        text = file.read()
    body = text[text.index('{', text.index('font[]')) + 1:]
    body = re.sub(r'//[^\n]*|/\*.*?\*/', '', body[:body.index('};')], flags=re.S)
    values = [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]{2}', body)]
    return [values[i:i + 5] for i in range(0, len(values) - 4, 5)]


def write(image, glyphs, text, left, top, size, colour):
    for index, char in enumerate(text):
        for column, bits in enumerate(glyphs[ord(char)]):
            for row in range(8):
                if bits >> row & 1:
                    x, y = left + (index * 6 + column) * size, top + row * size
                    image.paste(colour, (x, y, x + size, y + size))


def bounce(glyphs, name):
    """One frame of the screensaver, as drawBounce() lays it out."""
    image = Image.new('RGB', (320, 172))
    draw = ImageDraw.Draw(image)
    eye_w, eye_h, gap, size = 10, 18, 6, 3
    width, height = 8 + 2 * eye_w + gap + 10 + len(name) * 6 * size + 6, 8 * size + 14
    left, top = 96, 58
    draw.rounded_rectangle((left, top, left + width - 1, top + height - 1), 6, outline=TEAL, width=2)
    for eye in range(2):
        x, y = left + 8 + eye * (eye_w + gap), top + (height - eye_h) // 2
        draw.rounded_rectangle((x, y, x + eye_w - 1, y + eye_h - 1), 3, fill=TEAL)
    write(image, glyphs, name, left + 8 + 2 * eye_w + gap + 10, top + 7, size, TEAL)
    return image


STYLES = ['classic', 'cat', 'robot', 'minimal']
ROW = ['mood_normal', 'mood_happy', 'mood_angry', 'mood_sleepy', 'birthday', 'wave']
MOODS = ['normal', 'happy', 'surprised', 'sleepy', 'angry', 'sad', 'excited', 'love', 'suspicious']
STATUSES = ['night', 'lunch', 'away', 'log_hours', 'ci_red', 'locked_in', 'focus', 'focused', 'claude_needs_you', 'claude_done',
            'on_air', 'meeting_soon', 'dizzy', 'unimpressed', 'greeting', 'birthday', 'anniversary', 'stretch', 'water',
            'lunch_time', 'weekend', 'welcome_back', 'message', 'wave']


def faces():
    """docs/images/faces.png (one row per style) and the sheet of every face per style, from the board screenshots."""
    gap = 8
    sheet = Image.new('RGB', (len(ROW) * (320 + gap) + gap, len(STYLES) * (172 + gap) + gap), (245, 244, 240))
    for row, style in enumerate(STYLES):
        for column, name in enumerate(ROW):
            sheet.paste(Image.open(os.path.join(BOARD, f'{style}_{name}.png')), (gap + column * (320 + gap), gap + row * (172 + gap)))
    sheet.save(os.path.join(ROOT, 'docs', 'images', 'faces.png'), optimize=True)
    across, cell_w, cell_h = 6, 320 + 12, 172 + 30
    for style in STYLES:
        names = ['mood_' + mood for mood in MOODS] + STATUSES
        page = Image.new('RGB', (across * cell_w + 12, (len(names) + across - 1) // across * cell_h + 12), (245, 244, 240))
        draw = ImageDraw.Draw(page)
        for index, name in enumerate(names):
            x, y = 12 + index % across * cell_w, 12 + index // across * cell_h
            page.paste(Image.open(os.path.join(BOARD, f'{style}_{name}.png')), (x, y))
            draw.text((x + 2, y + 174), name, fill=(40, 40, 46))
        page.save(os.path.join(ROOT, 'docs', 'images', 'faces', f'faces_{style}.png'), optimize=True)
    print('wrote docs/images/faces.png and docs/images/faces/faces_*.png')


def main():
    """The status pictures are board screenshots (docs/images/board, classic face); only the bounce is drawn here."""
    glyphs = font()
    made = []
    for name, frame in FRAMES:
        image = Image.open(os.path.join(BOARD, f'classic_{frame}.png')).convert('RGB')
        image.save(os.path.join(SHOTS, f'persona_{name}.png'))
        made.append(image)
    made.append(bounce(glyphs, 'Sam'))
    made[-1].save(os.path.join(SHOTS, 'persona_bounce.png'))
    # The sheet for the README: five across, two down, with a dark frame around each screen.
    cell_w, cell_h, pad = 320 + 16, 172 + 16, 8
    sheet = Image.new('RGB', (5 * cell_w + pad * 6, 2 * cell_h + pad * 3), (245, 244, 240))
    for index, image in enumerate(made):
        x, y = pad + (index % 5) * (cell_w + pad), pad + (index // 5) * (cell_h + pad)
        ImageDraw.Draw(sheet).rounded_rectangle((x, y, x + cell_w - 1, y + cell_h - 1), 10, fill=(24, 24, 27))
        sheet.paste(image, (x + 8, y + 8))
    sheet.save(os.path.join(ROOT, 'docs', 'images', 'personal.png'), optimize=True)
    print('copied', len(made) - 1, 'board screenshots, drew the bounce, and wrote docs/images/personal.png')
    faces()


if __name__ == '__main__':
    main()
