#!/usr/bin/env python3
"""Turn any GIF into one the Monitor Buddy plays well.

    python tools/buddy_gif.py IN.gif OUT.gif [BACKGROUND]

BACKGROUND is the colour behind a see-through GIF, as RRGGBB (default 000000).

The Buddy's screen is 320x172. It draws a GIF 4, 2 or 1 times enlarged,
whichever fills the screen, so the picture is put on a canvas of 80x43, 160x86
or 320x172: the smallest it fits. A bigger one is shrunk to the screen.
"""
import sys

from PIL import Image, ImageSequence

CANVASES = [(80, 43), (160, 86), (320, 172)]
MIN_DELAY_MS = 20   # browsers play anything faster as 100 ms, and so do we


def save(path, frames, delays, colors=64, dither=False):
    """Write RGB frames, all of one CANVASES size, as a looping GIF with one
    palette. Returns the file size."""
    assert frames[0].size in CANVASES and all(frame.size == frames[0].size for frame in frames)
    step = max(1, len(frames) // 16)
    sample = Image.new('RGB', (frames[0].width, frames[0].height * len(frames[::step])))
    for row, frame in enumerate(frames[::step]):
        sample.paste(frame, (0, row * frame.height))
    palette = sample.quantize(colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    how = Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE
    out = [frame.quantize(palette=palette, dither=how) for frame in frames]
    out[0].save(path, save_all=True, append_images=out[1:], duration=delays, loop=0, disposal=1,
                optimize=False, interlace=False)
    with Image.open(path) as check:   # what a player will show is what was meant
        shown = [frame.convert('RGB') for frame in ImageSequence.Iterator(check)]
    assert len(shown) <= len(out) and shown[0].tobytes() == out[0].convert('RGB').tobytes(), path
    with open(path, 'rb') as file:
        return len(file.read())


def convert(source, path, background=(0, 0, 0), colors=64, dither=False):
    with Image.open(source) as gif:
        size = gif.size
        frames, delays = [], []
        for frame in ImageSequence.Iterator(gif):
            delay = frame.info.get('duration', 100)
            delays.append(delay if delay >= MIN_DELAY_MS else 100)
            picture = frame.convert('RGBA')
            flat = Image.new('RGB', size, background)
            flat.paste(picture, mask=picture)
            frames.append(flat)
    canvas = next((canvas for canvas in CANVASES if size[0] <= canvas[0] and size[1] <= canvas[1]), CANVASES[-1])
    if size[0] > canvas[0] or size[1] > canvas[1]:
        ratio = min(canvas[0] / size[0], canvas[1] / size[1])
        size = (max(1, round(size[0] * ratio)), max(1, round(size[1] * ratio)))
        frames = [frame.resize(size, Image.Resampling.LANCZOS) for frame in frames]
    placed = []
    for frame in frames:
        sheet = Image.new('RGB', canvas, background)
        sheet.paste(frame, ((canvas[0] - size[0]) // 2, (canvas[1] - size[1]) // 2))
        placed.append(sheet)
    return save(path, placed, delays, colors, dither)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    colour = bytes.fromhex(sys.argv[3]) if len(sys.argv) > 3 else bytes(3)
    print(convert(sys.argv[1], sys.argv[2], tuple(colour)), 'bytes')
