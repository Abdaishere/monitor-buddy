"""Downloads the Poly Haven (CC0) HDRIs and wood texture the product renders use into ./assets (not kept in git)."""
import os, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
URLS = [
    'https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/2k/studio_small_08_2k.hdr',
    'https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/2k/brown_photostudio_02_2k.hdr',
    'https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/wood_table_001/wood_table_001_diff_2k.jpg',
    'https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/wood_table_001/wood_table_001_rough_2k.jpg',
    'https://dl.polyhaven.org/file/ph-assets/Textures/jpg/2k/wood_table_001/wood_table_001_nor_gl_2k.jpg',
]
os.makedirs(os.path.join(HERE, 'assets'), exist_ok=True)
for u in URLS:
    dst = os.path.join(HERE, 'assets', u.rsplit('/', 1)[1])
    if not os.path.exists(dst):
        req = urllib.request.Request(u, headers={'User-Agent': 'blender-render'})
        with urllib.request.urlopen(req) as r, open(dst, 'wb') as f:
            f.write(r.read())
    print(dst, os.path.getsize(dst))
