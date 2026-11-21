"""Studio renders of the dock for the listing (docs/blog/images/render-*.jpg).

    EGL_DEVICE_ID=1 uv run --no-project --python 3.12 --with trimesh --with "pyrender>=0.1.45,<0.2" --with "Pillow>=10,<13" --with "numpy<2" --with networkx --with scipy --with shapely --with mapbox_earcut python docs/blog/render_dock.py docs/blog/images

Reads the model files from the dock project folder (D below); they are not in this repository.
"""
import os, sys, numpy as np, trimesh
os.environ.setdefault("PYOPENGL_PLATFORM", "egl")
import pyrender
from PIL import Image, ImageDraw, ImageFont
D = os.environ["DOCK_OUT"]  # the folder with the dock's STL files
OUT = sys.argv[1]
W, H, SS = 1600, 1200, 2
PLA = [0.17, 0.17, 0.185, 1]; BOARD = [0.03, 0.03, 0.035, 1]; SCREEN = [0.10, 0.16, 0.30, 1]; PLUG = [0.62, 0.63, 0.66, 1]
BG = [0.93, 0.93, 0.94, 1]

def ld(n):
    m = trimesh.load(D + n); m.fix_normals(); return m

def mat(c, rough=0.45, metal=0.0):
    return pyrender.MetallicRoughnessMaterial(baseColorFactor=c, metallicFactor=metal, roughnessFactor=rough, doubleSided=True)

def look(eye, target, up=(0, 0, 1)):
    eye, target, up = map(lambda v: np.array(v, float), (eye, target, up))
    f = target - eye; f /= np.linalg.norm(f)
    r = np.cross(f, up); r /= np.linalg.norm(r); u = np.cross(r, f)
    p = np.eye(4); p[:3, 0], p[:3, 1], p[:3, 2], p[:3, 3] = r, u, -f, eye
    return p

def render(items, direction, name, label, floor_z, fov=22.0, extra_floor=None, zoom=0.88):
    sc = pyrender.Scene(bg_color=BG, ambient_light=[0.22, 0.22, 0.23])
    bs = []
    for m, c, rough in items:
        sc.add(pyrender.Mesh.from_trimesh(m, smooth=False, material=mat(c, rough))); bs.append(m.bounds)
    b = np.array(bs); lo, hi = b[:, 0].min(0), b[:, 1].max(0); ctr = (lo + hi) / 2; rad = np.linalg.norm(hi - lo) / 2
    fl = trimesh.creation.box((4000, 4000, 1)); fl.apply_translation([ctr[0], ctr[1], floor_z - 0.5])
    top = floor_z if extra_floor is None else extra_floor[2]
    from shapely.geometry import MultiPoint
    import trimesh.creation as tc
    pts = np.vstack([m.vertices[m.vertices[:, 2] < top + 14][:, :2] for m, c, r in items])
    hull = MultiPoint(pts[::3]).convex_hull
    for i, off in enumerate(np.linspace(7, -2, 40)):
        poly = hull.buffer(off)
        sm = tc.extrude_polygon(poly, 0.01); sm.apply_translation([0, 0, top + 0.02 + i * 0.03])
        sc.add(pyrender.Mesh.from_trimesh(sm, smooth=False, material=pyrender.MetallicRoughnessMaterial(baseColorFactor=[0.05, 0.05, 0.07, 0.035], alphaMode='BLEND', roughnessFactor=1.0, metallicFactor=0.0)))
    if extra_floor is not None:
        sc.add(pyrender.Mesh.from_trimesh(extra_floor[0], smooth=False, material=pyrender.MetallicRoughnessMaterial(baseColorFactor=extra_floor[1], emissiveFactor=[0.25,0.2,0.1], roughnessFactor=0.9, metallicFactor=0.0)))
    d = np.array(direction, float); d /= np.linalg.norm(d)
    dist = rad / np.sin(np.radians(fov / 2)) * zoom
    cam = pyrender.PerspectiveCamera(yfov=np.radians(fov), aspectRatio=W / H, znear=1, zfar=dist * 6)
    cp = look(ctr + d * dist, ctr); sc.add(cam, pose=cp)
    # key (shadow), fill, rim: all relative to the camera so every view is lit alike
    r, u, back = cp[:3, 0], cp[:3, 1], cp[:3, 2]
    for dv, inten in ((back * 0.6 + r * 0.7 + np.array([0, 0, 1.0]), 11.0), (back * 0.8 - r * 0.9 + np.array([0, 0, 0.35]), 2.2), (-back * 0.9 + np.array([0, 0, 0.8]), 5.0)):
        sc.add(pyrender.DirectionalLight(color=[1, 1, 1], intensity=inten), pose=look(ctr + dv / np.linalg.norm(dv) * dist, ctr, up=(0.01, 0.02, 1)))
    rd = pyrender.OffscreenRenderer(W * SS, H * SS)
    col, _ = rd.render(sc); rd.delete()
    im = Image.fromarray(col).resize((W, H), Image.Resampling.LANCZOS)
    dr = ImageDraw.Draw(im)
    f = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 26)
    dr.text((40, H - 62), label, font=f, fill=(95, 95, 100))
    dr.text((W - 40, H - 62), '3D render', font=f, fill=(150, 150, 155), anchor='ra')
    im.save(os.path.join(OUT, name), quality=92, optimize=True); print(name)

head, base, board, plug = ld('dock_head_v8.stl'), ld('dock_base_v8.stl'), ld('mates/board.stl'), ld('mates/plug_c.stl')
fz = base.bounds[0][2]
asm = [(head, PLA, 0.42), (base, PLA, 0.42), (board, BOARD, 0.25), (plug, PLUG, 0.35)]
render(asm, (0.0, -1, 0.22), 'render-1-front.jpg', 'Front', fz)
render(asm, (0.75, -1, 0.45), 'render-2-three-quarter.jpg', 'Three-quarter view', fz)
render(asm, (1, -0.02, 0.15), 'render-3-side.jpg', 'Side: the screen leans back', fz)
render(asm, (-0.25, 1, 0.35), 'render-4-back.jpg', 'Back', fz)
render([(head, PLA, 0.42), (base, PLA, 0.42)], (-0.8, -1, 0.75), 'render-5-parts-only.jpg', 'Printed parts only, docked', fz)
def sh(m, v):
    m = m.copy(); m.apply_translation(v); return m
up = np.array([0, 0, 1.0])
# board slides out along the pocket's long axis (+X is the USB end)
ex = [(base, PLA, 0.42), (sh(head, up * 26), PLA, 0.42), (sh(board, up * 26 + np.array([0, -46.0, 6])), BOARD, 0.25), (sh(plug, up * 26 + np.array([26.0, 0, 0])), PLUG, 0.35)]
render(ex, (0.7, -1, 0.5), 'render-6-exploded.jpg', 'Exploded: base, head, board, USB-C plug', fz)
ph, pb = ld('print/dock_head_v8.stl'), ld('print/dock_base_v8.stl')
ph.apply_translation([8, 46, 0]); pb.apply_translation([0, 0, 0])
bed = trimesh.creation.box((110, 120, 2)); bed.apply_translation([30, 40, -1.0])
render([(ph, PLA, 0.42), (pb, PLA, 0.42)], (0.8, -1, 0.7), 'render-7-print-pose.jpg', 'As printed: base flat, head standing on its end', -2.0, extra_floor=(bed, [0.80, 0.66, 0.36, 1], 0.0), zoom=1.3)
