"""Adds the small "3D render" corner mark to the product renders and writes them as JPEG.

    python label.py <folder with raw renders> <output folder>
"""
import glob, os, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageStat

src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
for f in sorted(glob.glob(os.path.join(src, 'product-*.jpg'))):
    im = Image.open(f).convert('RGB')
    w, h = im.size
    # camera finish: a slight vignette and fine grain, so the frame does not look computer-clean
    yy, xx = np.mgrid[0:h, 0:w]
    r2 = ((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2
    a = np.asarray(im, np.float32) * (1 - 0.07 * np.clip(r2 / 2, 0, 1) ** 1.5)[..., None]
    a += np.random.default_rng(1).normal(0, 1.6, (h, w, 1))
    im = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    size = round(h * 0.022)
    font = None
    for p in ('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 'C:/Windows/Fonts/segoeui.ttf', 'arial.ttf'):
        try:
            font = ImageFont.truetype(p, size); break
        except OSError:
            pass
    font = font or ImageFont.load_default()
    text = '3D render'
    tw = ImageDraw.Draw(im).textlength(text, font=font)
    x, y = w - tw - round(h * 0.025), h - size - round(h * 0.028)
    lum = ImageStat.Stat(im.crop((int(x), int(y), w, h)).convert('L')).mean[0]
    over = Image.new('RGBA', im.size, (0, 0, 0, 0))
    ImageDraw.Draw(over).text((x, y), text, font=font, fill=(40, 40, 40, 170) if lum > 120 else (235, 235, 235, 170))
    im = Image.alpha_composite(im.convert('RGBA'), over).convert('RGB')
    im.save(os.path.join(dst, os.path.basename(f)), quality=92, optimize=True, progressive=True)
    print(os.path.basename(f), im.size)
