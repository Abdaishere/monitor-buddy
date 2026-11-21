#!/usr/bin/env python3
"""Draw the Monitor Buddy's own animations into a folder.

    python tools/buddy_art.py OUT_DIR [NYAN_SPRITE.gif]

They are made for the 320x172 screen, in the Buddy's GIF sizes (see
buddy_gif.py): six for the animation page and the three reactions
(full-day, ci-green, focus-done). Everything here is drawn by this script and
ships with the project.

With a sprite GIF of your own as the second argument it also builds a
nyan-cat.gif scene around it (rainbow, stars and sky). That sprite is not part
of this project: the Nyan Cat belongs to Chris Torres (nyan.cat).
"""
import os
import random
import sys

from buddy_gif import save
from PIL import Image, ImageDraw, ImageFont, ImageSequence

BIG, HALF, QUARTER = (320, 172), (160, 86), (80, 43)
NAME = 'BUDDY'
ODOO, TEAL, DARK = (113, 75, 103), (74, 195, 203), (33, 24, 33)
MONO = '/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf'


def font(size):
    return ImageFont.truetype(MONO, size)


def canvas(size, colour=(0, 0, 0)):
    image = Image.new('RGB', size, colour)
    draw = ImageDraw.Draw(image)
    draw.fontmode = '1'   # hard pixel edges: fewer colours, smaller files
    return image, draw


def wave(turns):
    """0..1..0 over a whole number of turns: a bounce between two walls."""
    return 1 - abs(2 * (turns % 1) - 1)


def bounce(path):
    """The Buddy's logo (its two eyes and its name) bounces like the logo of an
    idle DVD player. It changes colour at every wall and lands exactly in a
    corner twice a loop."""
    colours = [TEAL, (203, 166, 189), (255, 196, 0), (255, 99, 71), (120, 220, 120), (135, 160, 255)]
    letters = font(26)
    box = letters.getbbox(NAME)
    eye_w, eye_h, gap = 10, 18, 6   # the two rounded eyes of the favicon
    face = 2 * eye_w + gap
    width = face + 10 + box[2] - box[0] + 16
    height = max(eye_h, box[3] - box[1]) + 14
    count, across, down = 240, 3, 5   # both odd: the two bounces peak together at half time
    frames, colour, last = [], 0, (0.0, 0.0)
    for index in range(count):
        at = (wave(index * across / count), wave(index * down / count))
        before = (wave((index - 1) * across / count), wave((index - 1) * down / count))
        if index and any((at[axis] - before[axis]) * (before[axis] - last[axis]) < 0 for axis in (0, 1)):
            colour += 1
        last = before
        image, draw = canvas(BIG)
        left, top = round(at[0] * (BIG[0] - width)), round(at[1] * (BIG[1] - height))
        draw.rounded_rectangle((left, top, left + width - 1, top + height - 1), 6, outline=colours[colour % len(colours)], width=2)
        ink = colours[colour % len(colours)]
        eye_top = top + (height - eye_h) // 2
        for eye in range(2):
            eye_left = left + 8 + eye * (eye_w + gap)
            draw.rounded_rectangle((eye_left, eye_top, eye_left + eye_w - 1, eye_top + eye_h - 1), 3, fill=ink)
        text_top = top + (height - (box[3] - box[1])) // 2 - box[1]
        draw.text((left + 8 + face + 10 - box[0], text_top), NAME, font=letters, fill=ink)
        frames.append(image)
    return save(path, frames, 50, colors=8)


def starfield(path):
    """Flying through space, as on a 1995 screen saver."""
    random.seed(7)
    count = 48
    stars = [(random.uniform(-1, 1), random.uniform(-1, 1), random.random()) for _ in range(90)]
    frames = []
    for index in range(count):
        image, draw = canvas(HALF)
        for x, y, phase in stars:
            depth = 1 - (phase + index / count) % 1   # 1 far away .. 0 at the glass
            px, py = int(HALF[0] / 2 + x / (depth + 0.08) * 34), int(HALF[1] / 2 + y / (depth + 0.08) * 22)
            if 0 <= px < HALF[0] and 0 <= py < HALF[1]:
                shade = int(255 * min(1, (1 - depth) * 1.6 + 0.15))
                size = 1 if depth > 0.35 else 2
                draw.rectangle((px, py, px + size - 1, py + size - 1), fill=(shade, shade, shade))
        frames.append(image)
    return save(path, frames, 60, colors=8)


def rain(path):
    """Green code running down the screen."""
    random.seed(3)
    count, cell = 48, 4
    columns = [(random.randrange(count), random.choice((1, 1, 2)), random.randrange(6, 16)) for _ in range(HALF[0] // cell)]
    glyphs = [[random.random() < 0.5 for _ in range(9)] for _ in range(24)]
    rows = HALF[1] // cell + 1
    frames = []
    for index in range(count):
        image, draw = canvas(HALF)
        for column, (start, speed, tail) in enumerate(columns):
            head = (start + index * speed) * (rows + 16) // count % (rows + 16)
            for row in range(rows):
                age = head - row
                if 0 <= age < tail:
                    colour = (190, 255, 190) if age == 0 else (0, 255 - age * 200 // tail, 60 - age * 50 // tail)
                    glyph = glyphs[(column * 7 + row * 3 + (index // 4 if age < 3 else 0)) % len(glyphs)]
                    for bit, lit in enumerate(glyph):
                        if lit:
                            draw.point((column * cell + bit % 3, row * cell + bit // 3), fill=colour)
        frames.append(image)
    return save(path, frames, 70, colors=16)


def fire(path):
    """The demo-scene fire, drawn 4 times enlarged."""
    random.seed(11)
    width, height = QUARTER[0], QUARTER[1] + 3
    heat = [[0] * width for _ in range(height)]
    raw = []
    for index in range(40 + 36):
        heat[-1] = [random.choice((0, 0, 255, 255, 255)) for _ in range(width)]
        for row in range(height - 1):
            for column in range(width):
                below = heat[row + 1]
                total = below[(column - 1) % width] + below[column] + below[(column + 1) % width] + heat[min(row + 2, height - 1)][column]
                heat[row][column] = max(0, total * 100 // 418 - (1 if random.random() < 0.35 else 0))
        if index >= 40:
            raw.append([line[:] for line in heat[:QUARTER[1]]])

    def colour(value):
        return (min(255, value * 3), min(255, max(0, value * 3 - 255)), min(255, max(0, value * 3 - 510)))

    frames = []
    for index in range(24):   # the last 12 fade into the first 12, so the loop has no seam
        blend = max(0, index - 11) / 12
        image = Image.new('RGB', QUARTER)
        image.putdata([colour(int(raw[index][row][column] * (1 - blend) + raw[index - 12][row][column] * blend) if blend else raw[index + 12][row][column])
                       for row in range(QUARTER[1]) for column in range(width)])
        frames.append(image)
    return save(path, frames, 70, colors=32)


def pong(path):
    """Two paddles that never miss."""
    count, across, down = 96, 2, 3
    frames = []
    for index in range(count):
        image, draw = canvas(HALF)
        x = int(6 + wave(index * across / count) * (HALF[0] - 15))
        y = int(2 + wave(index * down / count + 0.25) * (HALF[1] - 7))
        for dash in range(0, HALF[1], 8):
            draw.rectangle((79, dash, 80, dash + 3), fill=(70, 70, 70))
        for side, lag in ((2, 0.6), (HALF[0] - 5, 0.6)):
            near = 1 - abs((x - side) / HALF[0])
            paddle = int(min(max(y - 6 + (HALF[1] / 2 - y) * (1 - near) * lag, 0), HALF[1] - 15))
            draw.rectangle((side, paddle, side + 2, paddle + 14), fill=(255, 255, 255))
        draw.rectangle((x, y, x + 2, y + 2), fill=TEAL)
        frames.append(image)
    return save(path, frames, 40, colors=4)


def loading(path):
    """An install that reaches 99 % and thinks again."""
    title, small = font(15), font(12)
    steps = [(0, 'INSTALLING MOTIVATION'), (22, 'DOWNLOADING COFFEE'), (47, 'COMPILING EXCUSES'),
             (71, 'RUNBOT IS THINKING'), (90, 'ALMOST THERE'), (99, 'ALMOST THERE')]
    frames, delays = [], []
    for percent in list(range(0, 100, 3)) + [99] * 14:
        image, draw = canvas(BIG, DARK)
        label = [text for start, text in steps if percent >= start][-1]
        dots = '.' * (len(frames) // 3 % 4)
        draw.text((24, 40), label + dots, font=title, fill=(247, 243, 247))
        draw.rectangle((24, 78, 295, 104), outline=ODOO, width=2)
        draw.rectangle((28, 82, 28 + 263 * percent // 100, 100), fill=TEAL)
        draw.text((24, 116), f'{percent}%', font=small, fill=(173, 154, 165))
        frames.append(image)
        delays.append(110 if percent < 99 else 160)
    for blink in range(8):
        image, draw = canvas(BIG, DARK)
        draw.text((24, 40), 'ERROR 0x4D4F4E', font=title, fill=(255, 99, 71))
        draw.text((24, 70), 'IT IS MONDAY SOMEWHERE.', font=small, fill=(247, 243, 247))
        draw.text((24, 92), 'RETRYING' + ('_' if blink % 2 else ''), font=small, fill=(173, 154, 165))
        frames.append(image)
        delays.append(350)
    return save(path, frames, delays, colors=8)


def full_day(path):
    """The day's ring fills to 8 hours, then confetti. Plays when the hours pass a full day."""
    random.seed(21)
    title, small = font(22), font(13)
    colours = [TEAL, (203, 166, 189), (255, 196, 0), (255, 99, 71), (120, 220, 120)]
    bits = [(random.randrange(BIG[0]), random.randrange(-BIG[1], 0), random.randrange(2, 5), random.randrange(len(colours)),
             random.choice((1, 2))) for _ in range(46)]
    frames, delays = [], []
    for index in range(44):
        image, draw = canvas(BIG, DARK)
        fill = min(1.0, index / 16)
        draw.ellipse((22, 34, 126, 138), outline=(70, 56, 68), width=9)
        if fill > 0:
            draw.arc((22, 34, 126, 138), -90, -90 + int(360 * fill), fill=TEAL, width=9)
        hours = f'{int(8 * fill)}:00' if fill < 1 else '8:00'
        box = small.getbbox(hours)
        draw.text((74 - (box[2] - box[0]) // 2, 79), hours, font=small, fill=(247, 243, 247))
        if fill >= 1:
            if (index // 3) % 4 != 3:
                draw.text((146, 62), 'FULL DAY', font=title, fill=(255, 196, 0))
            draw.text((146, 94), 'HOURS LOGGED', font=small, fill=(173, 154, 165))
            for x, y, size, colour, speed in bits:
                top = (y + (index - 16) * 7 * speed) % (BIG[1] * 2) - BIG[1] // 2
                if 0 <= top < BIG[1]:
                    draw.rectangle((x, top, x + size, top + size), fill=colours[colour])
        frames.append(image)
        delays.append(70 if fill < 1 else 90)
    return save(path, frames, delays, colors=8)


def ci_green(path):
    """A tick draws itself and the checks go green. Plays when the last failing pull request passes."""
    green, dim = (120, 220, 120), (40, 78, 46)
    title, small = font(22), font(13)
    points = [(34, 88), (62, 116), (112, 58)]
    frames, delays = [], []
    for index in range(34):
        image, draw = canvas(BIG)
        draw.ellipse((18, 32, 128, 142), outline=dim if index < 12 else green, width=5)
        done = min(1.0, index / 11)
        first, second = min(1.0, done / 0.4), max(0.0, (done - 0.4) / 0.6)
        mid = (points[0][0] + (points[1][0] - points[0][0]) * first, points[0][1] + (points[1][1] - points[0][1]) * first)
        draw.line((points[0], mid), fill=green, width=9)
        if second > 0:
            end = (points[1][0] + (points[2][0] - points[1][0]) * second, points[1][1] + (points[2][1] - points[1][1]) * second)
            draw.line((points[1], end), fill=green, width=9)
        if index >= 12:
            draw.text((148, 54), 'ALL GREEN', font=title, fill=green)
            for row in range(3):
                if index >= 15 + row * 4:
                    draw.rectangle((148, 92 + row * 16, 156, 100 + row * 16), fill=green)
                    draw.text((164, 90 + row * 16), ('BUILD', 'TESTS', 'LINT')[row] + ' PASSED', font=small, fill=(173, 154, 165))
        frames.append(image)
        delays.append(60 if index < 12 else 120)
    return save(path, frames, delays, colors=8)


def focus_done(path):
    """Fireworks over FOCUS DONE. Plays after a long focus session."""
    random.seed(9)
    title = font(13)
    colours = [TEAL, (255, 196, 0), (255, 99, 71), (203, 166, 189), (120, 220, 120)]
    bursts = [(random.randrange(22, HALF[0] - 22), random.randrange(14, 44), start, colours[start // 6 % len(colours)])
              for start in range(0, 36, 6)]
    box = title.getbbox('FOCUS DONE')
    frames = []
    for index in range(36):
        image, draw = canvas(HALF)
        for x, y, start, colour in bursts:
            age = (index - start) % 36
            if age < 5:
                draw.rectangle((x, HALF[1] - 12 - (HALF[1] - 12 - y) * age // 5, x + 1, HALF[1] - 10 - (HALF[1] - 12 - y) * age // 5), fill=(120, 120, 120))
            elif age < 16:
                reach = (age - 4) * 2
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (0.7, 0.7), (-0.7, 0.7), (0.7, -0.7), (-0.7, -0.7)):
                    px, py = int(x + dx * reach), int(y + dy * reach + (age - 4) ** 2 / 14)
                    if age < 12 or (px + py + index) % 2:
                        draw.rectangle((px, py, px + 1, py + 1), fill=colour)
        draw.text(((HALF[0] - (box[2] - box[0])) // 2, HALF[1] - 15), 'FOCUS DONE', font=title, fill=(247, 243, 247))
        frames.append(image)
    return save(path, frames, 80, colors=8)


def nyan(path, sprite):
    """Nyan Cat's whole scene around the sprite: sky, rainbow and stars."""
    sky = (0, 51, 102)
    bands = [(255, 0, 0), (255, 153, 0), (255, 255, 0), (51, 255, 0), (0, 153, 255), (102, 51, 255)]
    with Image.open(sprite) as gif:
        cats = [frame.convert('RGBA').resize((68, 42), Image.Resampling.NEAREST) for frame in ImageSequence.Iterator(gif)]
    random.seed(5)
    stars = [(random.randrange(HALF[0]), random.randrange(4, HALF[1] - 4), random.randrange(6)) for _ in range(9)]
    frames = []
    for index, cat in enumerate(cats):
        image, draw = canvas(HALF, sky)
        for x, y, phase in stars:
            px, size = (x - index * HALF[0] // len(cats)) % HALF[0], (phase + index) % 6
            reach = (0, 1, 2, 3, 2, 1)[size]
            draw.point((px, y), fill=(255, 255, 255))
            for arm in range(1, reach + 1):
                if arm != 2 or reach < 3:
                    draw.point([(px + arm, y), (px - arm, y), (px, y + arm), (px, y - arm)], fill=(255, 255, 255))
        cat_x, cat_y = 62, 22 + (1 if index % 6 in (1, 2, 3) else 0)
        for segment in range(0, cat_x + 12, 8):
            lift = 2 if (segment // 8 + index // 2) % 2 else 0
            for band, colour in enumerate(bands):
                draw.rectangle((cat_x + 10 - segment - 8, 26 + lift + band * 6, cat_x + 10 - segment, 26 + lift + band * 6 + 5), fill=colour)
        image.paste(cat, (cat_x, cat_y), cat)
        frames.append(image)
    return save(path, frames, 70, colors=32)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    made = {'bounce': bounce, 'starfield': starfield, 'code-rain': rain, 'fire': fire, 'pong': pong, 'loading': loading,
            'full-day': full_day, 'ci-green': ci_green, 'focus-done': focus_done}
    os.makedirs(sys.argv[1], exist_ok=True)
    for name, draw_it in made.items():
        print(f'{name}.gif', draw_it(os.path.join(sys.argv[1], name + '.gif')), 'bytes')
    if len(sys.argv) > 2:
        print('nyan-cat.gif', nyan(os.path.join(sys.argv[1], 'nyan-cat.gif'), sys.argv[2]), 'bytes')
