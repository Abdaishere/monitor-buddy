"""Photoreal product renders of Monitor Buddy (docs/blog/images/product-*.jpg). Blender 4.2+ / 5.x, Cycles.

    blender -b -P docs/blog/render/render_product.py -- --stl <folder with dock_head_v8.stl, dock_base_v8.stl> \
        --out <folder> [--shots hero,front,...] [--res 1600x1200] [--samples 384] [--suffix -sq] [--blend scene.blend]

The dock's model files are not in this repository (MakerWorld licence), so --stl points at them.
The board, glass, USB-C cable, keyboard and monitor are modelled here. Screen pictures are the firmware
screenshots in docs/guide/shots. HDRIs and the wood texture come from Poly Haven (CC0): run fetch_assets.py first.
The "3D render" corner mark is added afterwards by label.py.
"""
import bpy, bmesh, sys, os, math
import numpy as np
from mathutils import Vector, Matrix

HERE = os.path.dirname(os.path.abspath(__file__))
SHOTS_DIR = os.path.normpath(os.path.join(HERE, '..', '..', 'guide', 'shots'))
ASSETS = os.path.join(HERE, 'assets')
MM = 0.001


def arg(name, default=None):
    a = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    return a[a.index(name) + 1] if name in a else default


STL = arg('--stl', os.environ.get('DOCK_OUT', '.'))  # the folder with the dock's STL files
OUT = arg('--out', os.path.join(HERE, 'out'))
RES = [int(v) for v in arg('--res', '1600x1200').split('x')]
SAMPLES = int(arg('--samples', '384'))
SUFFIX = arg('--suffix', '')
BLEND = arg('--blend')
CLIPS_ARG = arg('--clips')
STEP = int(arg('--step', '1'))
FPS = 30

# Board pose in the dock's frame (from the dock CAD's board stand-in): long axis X, leaning back 28 degrees.
B = Matrix(((1, 0, 0, 0), (0, 0.46933, -0.88302, -0.004095), (0, 0.88302, 0.46933, 0.056817), (0, 0, 0, 1)))
BW, BH, BT = 44.5 * MM, 24.55 * MM, 10.6 * MM       # Waveshare ESP32-C6-Touch-LCD-1.47
AW, AH = 32.4 * MM, 17.4 * MM                       # lit area of the 1.47in panel (320 x 172)
DECK = 24.3 * MM                                    # the dock's base sits on the keyboard's back edge at this height


# ---------------------------------------------------------------- helpers
def V(x, y, z):
    return Vector((x * MM, y * MM, z * MM))


def T(x, y, z):
    return Matrix.Translation(V(x, y, z))


def link(o):
    bpy.context.scene.collection.objects.link(o)
    return o


def smooth(me, angle=35):
    for p in me.polygons:
        p.use_smooth = True
    try:
        me.set_sharp_from_angle(angle=math.radians(angle))
    except Exception:
        pass


def obj_from_bm(bm, name, mat=None, angle=35):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    smooth(me, angle)
    o = link(bpy.data.objects.new(name, me))
    if mat:
        me.materials.append(mat)
    return o


def rbox(name, sx, sy, sz, r_corner=0.0, r_edge=0.0, mat=None, seg=8):
    """Box centred on the origin; r_corner rounds the four edges along Z, r_edge softens the rest."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x *= sx; v.co.y *= sy; v.co.z *= sz
    if r_corner > 0:
        es = [e for e in bm.edges if abs(e.verts[0].co.z - e.verts[1].co.z) > 1e-9]
        bmesh.ops.bevel(bm, geom=es, offset=r_corner, segments=seg, profile=0.5, affect='EDGES')
    if r_edge > 0:
        es = [e for e in bm.edges if abs(abs(e.verts[0].co.z) - sz / 2) < 1e-9 and abs(abs(e.verts[1].co.z) - sz / 2) < 1e-9]
        bmesh.ops.bevel(bm, geom=es, offset=r_edge, segments=4, profile=0.5, affect='EDGES')
    return obj_from_bm(bm, name, mat)


def rprism(name, w, h, length, r, r_end, mat=None, n=12, ne=5):
    """Rounded-rectangle bar along +X (centred), corner radius r, ends rounded by r_end. Clean quad rings."""
    prof = []
    for cx, cy, a0 in ((w / 2 - r, h / 2 - r, 0), (-w / 2 + r, h / 2 - r, 90), (-w / 2 + r, -h / 2 + r, 180), (w / 2 - r, -h / 2 + r, 270)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            prof.append((cx + r * math.cos(a), cy + r * math.sin(a), math.cos(a), math.sin(a)))
    stations = [(-length / 2 + r_end - r_end * math.cos(math.radians(90 * i / ne)), r_end - r_end * math.sin(math.radians(90 * i / ne))) for i in range(ne + 1)]
    stations += [(-x, d) for x, d in reversed(stations)]
    bm = bmesh.new()
    rings = [[bm.verts.new((x, py - ny * d, pz - nz * d)) for (py, pz, ny, nz) in prof] for x, d in stations]
    m = len(prof)
    for a, b in zip(rings, rings[1:]):
        for i in range(m):
            bm.faces.new((a[i], a[(i + 1) % m], b[(i + 1) % m], b[i]))
    bm.faces.new(rings[0]); bm.faces.new(rings[-1][::-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    return obj_from_bm(bm, name, mat, 40)


def empty(name, M=None):
    e = link(bpy.data.objects.new(name, None))
    if M is not None:
        e.matrix_world = M
    return e


def parent(o, p):
    o.parent = p
    return o


def sock(node, names, out=False):
    coll = node.outputs if out else node.inputs
    for n in names:
        if n in coll:
            return coll[n]
    raise KeyError(names)


def new_mat(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    b = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    return m, nt, b


def setp(b, **kw):
    alias = {'base': ['Base Color'], 'rough': ['Roughness'], 'metal': ['Metallic'], 'emit': ['Emission Color', 'Emission'],
             'emit_s': ['Emission Strength'], 'coat': ['Coat Weight', 'Clearcoat'], 'coat_rough': ['Coat Roughness', 'Clearcoat Roughness'],
             'sss': ['Subsurface Weight', 'Subsurface'], 'spec': ['Specular IOR Level', 'Specular'], 'ior': ['IOR']}
    for k, v in kw.items():
        s = sock(b, alias[k])
        s.default_value = (*v, 1) if isinstance(v, (tuple, list)) and len(v) == 3 else v


def img(path, colorspace='sRGB'):
    im = bpy.data.images.load(path, check_existing=True)
    im.colorspace_settings.name = colorspace
    return im


# ---------------------------------------------------------------- materials
def mat_pla(axis):
    """Black silk PLA: satin, with 0.16 mm layer lines across `axis` of the part and a little surface grain."""
    m, nt, b = new_mat('PLA_' + 'XYZ'[axis])
    N, L = nt.nodes, nt.links
    tc = N.new('ShaderNodeTexCoord')
    sep = N.new('ShaderNodeSeparateXYZ'); L.new(tc.outputs['Object'], sep.inputs[0])
    wob = N.new('ShaderNodeTexNoise'); wob.inputs['Scale'].default_value = 90; L.new(tc.outputs['Object'], wob.inputs['Vector'])
    k = N.new('ShaderNodeMath'); k.operation = 'MULTIPLY'; k.inputs[1].default_value = 2 * math.pi / (0.16 * MM)
    L.new(sep.outputs[axis], k.inputs[0])
    add = N.new('ShaderNodeMath'); add.operation = 'ADD'; L.new(k.outputs[0], add.inputs[0]); L.new(wob.outputs[0], add.inputs[1])
    sn = N.new('ShaderNodeMath'); sn.operation = 'SINE'; L.new(add.outputs[0], sn.inputs[0])
    b1 = N.new('ShaderNodeBump'); b1.inputs['Strength'].default_value = 0.65; b1.inputs['Distance'].default_value = 0.00004
    L.new(sn.outputs[0], b1.inputs['Height'])
    grain = N.new('ShaderNodeTexNoise'); grain.inputs['Scale'].default_value = 1400; grain.inputs['Detail'].default_value = 3
    L.new(tc.outputs['Object'], grain.inputs['Vector'])
    b2 = N.new('ShaderNodeBump'); b2.inputs['Strength'].default_value = 0.10; b2.inputs['Distance'].default_value = 0.00005
    L.new(grain.outputs[0], b2.inputs['Height']); L.new(b1.outputs[0], b2.inputs['Normal'])
    bev = N.new('ShaderNodeBevel'); bev.inputs['Radius'].default_value = 0.00035; bev.samples = 6
    L.new(b2.outputs[0], bev.inputs['Normal']); L.new(bev.outputs[0], b.inputs['Normal'])
    blot = N.new('ShaderNodeTexNoise'); blot.inputs['Scale'].default_value = 220; L.new(tc.outputs['Object'], blot.inputs['Vector'])
    mr = N.new('ShaderNodeMapRange'); mr.inputs['To Min'].default_value = 0.30; mr.inputs['To Max'].default_value = 0.46
    L.new(blot.outputs[0], mr.inputs[0]); L.new(mr.outputs[0], b.inputs['Roughness'])
    setp(b, base=(0.014, 0.014, 0.016))
    return m


def mat_plain(name, base, rough, **kw):
    m, nt, b = new_mat(name)
    setp(b, base=base, rough=rough, **kw)
    return m


def mat_screen(image_path, strength):
    """Cover glass over a lit LCD: black gloss, the firmware screenshot glowing in the active area."""
    m, nt, b = new_mat('Screen')
    N, L = nt.nodes, nt.links
    tc = N.new('ShaderNodeTexCoord')
    mp = N.new('ShaderNodeMapping'); mp.inputs['Scale'].default_value = (1 / AW, 1 / AH, 1); mp.inputs['Location'].default_value = (0.5, 0.5, 0)
    L.new(tc.outputs['Object'], mp.inputs['Vector'])
    tx = N.new('ShaderNodeTexImage'); tx.image = img(image_path); tx.interpolation = 'Closest'; tx.extension = 'CLIP'
    L.new(mp.outputs[0], tx.inputs['Vector'])
    # rounded-corner mask of the lit area
    r = 2.0 * MM
    ab = N.new('ShaderNodeVectorMath'); ab.operation = 'ABSOLUTE'; L.new(tc.outputs['Object'], ab.inputs[0])
    sb = N.new('ShaderNodeVectorMath'); sb.operation = 'SUBTRACT'; sb.inputs[1].default_value = (AW / 2 - r, AH / 2 - r, 1.0)
    L.new(ab.outputs[0], sb.inputs[0])
    mx = N.new('ShaderNodeVectorMath'); mx.operation = 'MAXIMUM'; mx.inputs[1].default_value = (0, 0, 0); L.new(sb.outputs[0], mx.inputs[0])
    ln = N.new('ShaderNodeVectorMath'); ln.operation = 'LENGTH'; L.new(mx.outputs[0], ln.inputs[0])
    lt = N.new('ShaderNodeMath'); lt.operation = 'LESS_THAN'; lt.inputs[1].default_value = r; L.new(ln.outputs['Value'], lt.inputs[0])
    # an LCD's black is not black: a faint blue backlight glow, as in the real photo
    lift = N.new('ShaderNodeMixRGB'); lift.blend_type = 'ADD'; lift.inputs[0].default_value = 1.0
    lift.inputs[2].default_value = (0.007, 0.012, 0.034, 1); L.new(tx.outputs['Color'], lift.inputs[1])
    mul = N.new('ShaderNodeMixRGB'); mul.blend_type = 'MULTIPLY'; mul.inputs[0].default_value = 1.0
    L.new(lift.outputs[0], mul.inputs[1]); L.new(lt.outputs[0], mul.inputs[2])
    L.new(mul.outputs[0], sock(b, ['Emission Color', 'Emission']))
    mat_screen.tex = tx
    smudge = N.new('ShaderNodeTexNoise'); smudge.inputs['Scale'].default_value = 160; smudge.inputs['Detail'].default_value = 4
    L.new(tc.outputs['Object'], smudge.inputs['Vector'])
    mr = N.new('ShaderNodeMapRange'); mr.inputs['From Min'].default_value = 0.45; mr.inputs['To Min'].default_value = 0.015; mr.inputs['To Max'].default_value = 0.07
    L.new(smudge.outputs[0], mr.inputs[0]); L.new(mr.outputs[0], b.inputs['Roughness'])
    setp(b, base=(0.004, 0.004, 0.005), emit_s=strength, ior=1.52)
    return m


def mat_emit(name, color, strength):
    m, nt, b = new_mat(name)
    setp(b, base=(0, 0, 0), rough=0.6, emit=color, emit_s=strength)
    return m


def mat_wood():
    m, nt, b = new_mat('Wood')
    N, L = nt.nodes, nt.links
    tc = N.new('ShaderNodeTexCoord')
    mp = N.new('ShaderNodeMapping'); mp.inputs['Scale'].default_value = (3.0, 3.0, 3.0); mp.inputs['Rotation'].default_value = (0, 0, math.radians(8))
    L.new(tc.outputs['Object'], mp.inputs['Vector'])
    d = N.new('ShaderNodeTexImage'); d.image = img(os.path.join(ASSETS, 'wood_table_001_diff_2k.jpg'))
    ro = N.new('ShaderNodeTexImage'); ro.image = img(os.path.join(ASSETS, 'wood_table_001_rough_2k.jpg'), 'Non-Color')
    no = N.new('ShaderNodeTexImage'); no.image = img(os.path.join(ASSETS, 'wood_table_001_nor_gl_2k.jpg'), 'Non-Color')
    for t in (d, ro, no):
        L.new(mp.outputs[0], t.inputs['Vector'])
    hsv = N.new('ShaderNodeHueSaturation'); hsv.inputs['Value'].default_value = 0.55; hsv.inputs['Saturation'].default_value = 0.9
    L.new(d.outputs[0], hsv.inputs['Color']); L.new(hsv.outputs[0], b.inputs['Base Color'])
    L.new(ro.outputs[0], b.inputs['Roughness'])
    nm = N.new('ShaderNodeNormalMap'); nm.inputs['Strength'].default_value = 0.8
    L.new(no.outputs[0], nm.inputs['Color']); L.new(nm.outputs[0], b.inputs['Normal'])
    return m


# ---------------------------------------------------------------- parts
def import_stl(name, mat):
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.wm.stl_import(filepath=os.path.join(STL, name), global_scale=MM)
    o = bpy.context.selected_objects[0]
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bm = bmesh.new(); bm.from_mesh(o.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bm.to_mesh(o.data); bm.free()
    smooth(o.data, 32)
    o.data.materials.append(mat)
    return o


def make_board(screen, strength, M):
    """The display unit: black body, glass front with the lit screen, two side buttons, USB-C socket on +X."""
    root = empty('board', M)
    body_m = mat_plain('BoardBody', (0.012, 0.012, 0.013), 0.32)
    s = 0.994
    body = rbox('board_body', BW * s, BH * s, BT - 1.0 * MM, 3.4 * MM, 0.5 * MM, body_m)
    body.location = (0, 0, -0.5 * MM)
    glass = rbox('board_glass', BW * s - 0.5 * MM, BH * s - 0.5 * MM, 1.0 * MM, 3.2 * MM, 0.35 * MM, mat_screen(os.path.join(SHOTS_DIR, screen), strength))
    # mesh in board coordinates so the screen picture is centred on the board
    for v in glass.data.vertices:
        v.co.z += BT / 2 - 0.5 * MM
    port = rbox('usb_socket', 2.0 * MM, 8.6 * MM, 2.8 * MM, 0, 0, mat_plain('PortDark', (0.002, 0.002, 0.002), 0.6))
    port.location = (BW / 2 - 0.9 * MM, 0, -2.55 * MM)
    kids = [body, glass, port]
    for sy in (-1, 1):
        bt = rbox('button', 5.0 * MM, 1.6 * MM, 2.4 * MM, 0, 0.3 * MM, body_m)
        bt.location = (14.35 * MM, sy * (BH / 2 - 0.3 * MM), -1.5 * MM)
        kids.append(bt)
    for k in kids:
        parent(k, root)
    return root


def make_plug(M):
    """USB-C plug in board coordinates (plugged into the +X end)."""
    root = empty('plug', M)
    white = mat_plain('CableWhite', (0.80, 0.80, 0.78), 0.45)
    steel = mat_plain('Steel', (0.72, 0.72, 0.74), 0.28, metal=1.0)
    z = -2.55 * MM
    tip = rprism('plug_tip', 8.3 * MM, 2.5 * MM, 7.0 * MM, 1.2 * MM, 0.2 * MM, steel); tip.location = (BW / 2 + 0.5 * MM, 0, z)
    over = rprism('plug_overmold', 11.6 * MM, 5.4 * MM, 17.0 * MM, 2.3 * MM, 1.0 * MM, white); over.location = (BW / 2 + 1.6 * MM + 8.5 * MM, 0, z)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=32, radius1=2.7 * MM, radius2=2.0 * MM, depth=8.0 * MM)
    relief = obj_from_bm(bm, 'plug_relief', white, 50)
    relief.rotation_euler = (0, math.radians(90), 0); relief.location = (BW / 2 + 18.6 * MM + 4.0 * MM, 0, z)
    for k in (tip, over, relief):
        parent(k, root)
    exit_local = Vector((BW / 2 + 26.0 * MM, 0, z))
    return root, white, exit_local


def make_cable(points, mat, r=1.75 * MM):
    cu = bpy.data.curves.new('cable', 'CURVE'); cu.dimensions = '3D'
    sp = cu.splines.new('BEZIER'); sp.bezier_points.add(len(points) - 1)
    for bp, p in zip(sp.bezier_points, points):
        bp.co = p; bp.handle_left_type = bp.handle_right_type = 'AUTO'
    cu.bevel_depth = r; cu.bevel_resolution = 8; cu.resolution_u = 32; cu.use_fill_caps = True
    o = link(bpy.data.objects.new('cable', cu)); cu.materials.append(mat)
    return o


def cable_from(Mplug, exit_local, mat, waypoints, R=Matrix.Identity(4)):
    """Cable leaving the plug straight, then through world waypoints (mm)."""
    p0 = Mplug @ exit_local
    d = (Mplug.to_3x3() @ Vector((1, 0, 0))).normalized()
    pts = [p0 - d * 3 * MM, p0 + d * 10 * MM] + [R @ V(*w) for w in waypoints]
    return make_cable(pts, mat)


def rest_matrix(o, down):
    """Matrix that lays object `o` on the ground on its largest flat side facing `down` (object space)."""
    bm = bmesh.new(); bm.from_mesh(o.data)
    hull = bmesh.ops.convex_hull(bm, input=bm.verts)
    hf = [g for g in hull['geom'] if isinstance(g, bmesh.types.BMFace)]
    com = sum((v.co for v in bm.verts), Vector()) / len(bm.verts)
    best, groups = None, {}
    for f in hf:
        n = f.normal
        if n.dot(down) < 0.6:
            continue
        key = (round(n.x, 1), round(n.y, 1), round(n.z, 1))
        g = groups.setdefault(key, [0.0, Vector(), 0.0])
        g[0] += f.calc_area(); g[1] += n * f.calc_area(); g[2] = max(g[2], max(v.co.dot(n) for v in f.verts))
    for key, (area, nsum, dist) in groups.items():
        if best is None or area > best[0]:
            best = (area, nsum.normalized(), dist)
    bm.free()
    n = best[1]
    R = n.rotation_difference(Vector((0, 0, -1))).to_matrix().to_4x4()
    return Matrix.Translation((0, 0, best[2])) @ R


class Dock:
    """Base + head + board + plug, each on its own empty. G places the dock frame in the world."""

    def __init__(self, G, screen='page_date.png', strength=3.0, board=True, plug=True, R=Matrix.Identity(4),
                 head_off=(0, 0, 0), board_off=(0, 0, 0), plug_off=(0, 0, 0)):
        self.G = G; self.R = R
        self.base_root = empty('base_root', G); self.base = parent(import_stl('dock_base_v8.stl', mat_pla(2)), self.base_root)
        off = lambda v: Matrix.Translation(R.to_3x3() @ V(*v))
        Gh = off(head_off) @ G
        self.head_root = empty('head_root', Gh); self.head = parent(import_stl('dock_head_v8.stl', mat_pla(0)), self.head_root)
        self.Mb = off(board_off) @ Gh @ B
        self.board = make_board(screen, strength, self.Mb) if board else None
        self.Mp = off(plug_off) @ self.Mb
        if plug:
            self.plug, self.white, self.exit = make_plug(self.Mp)

    def pose(self, head_off=(0, 0, 0), board_off=(0, 0, 0), plug_off=(0, 0, 0), G=None, R=None):
        """Move the parts (used by the clips)."""
        if G is not None:
            self.G = G
        if R is not None:
            self.R = R
        off = lambda v: Matrix.Translation(self.R.to_3x3() @ V(*v))
        self.base_root.matrix_world = self.G
        Gh = off(head_off) @ self.G
        self.head_root.matrix_world = Gh
        self.Mb = off(board_off) @ Gh @ B
        if self.board:
            self.board.matrix_world = self.Mb
        self.Mp = off(plug_off) @ self.Mb
        if hasattr(self, 'plug'):
            self.plug.matrix_world = self.Mp

    def cable(self, waypoints):
        return cable_from(self.Mp, self.exit, self.white, waypoints, self.R)


# ---------------------------------------------------------------- sets
def world(hdr, strength, rot=0.0):
    w = bpy.data.worlds.new('World'); bpy.context.scene.world = w; w.use_nodes = True
    N, L = w.node_tree.nodes, w.node_tree.links
    bg = next(n for n in N if n.type == 'BACKGROUND')
    tc = N.new('ShaderNodeTexCoord'); mp = N.new('ShaderNodeMapping'); mp.inputs['Rotation'].default_value = (0, 0, math.radians(rot))
    env = N.new('ShaderNodeTexEnvironment'); env.image = img(os.path.join(ASSETS, hdr), 'Linear Rec.709')
    L.new(tc.outputs['Generated'], mp.inputs[0]); L.new(mp.outputs[0], env.inputs[0]); L.new(env.outputs[0], bg.inputs['Color'])
    bg.inputs['Strength'].default_value = strength


def area(name, loc, target, size, power, color=(1, 1, 1), size_y=None, spec=1.0):
    l = bpy.data.lights.new(name, 'AREA'); l.energy = power; l.color = color
    l.shape = 'RECTANGLE'; l.size = size; l.size_y = size_y or size
    o = link(bpy.data.objects.new(name, l)); o.location = V(*loc)
    o.rotation_euler = (V(*target) - V(*loc)).to_track_quat('-Z', 'Y').to_euler()
    o.visible_camera = False
    l.specular_factor = spec
    return o


def camera(loc, target, lens=85, fstop=8, focus=None):
    c = bpy.data.cameras.new('Cam'); c.lens = lens; c.sensor_width = 36; c.clip_start = 0.005; c.clip_end = 50
    c.dof.use_dof = True; c.dof.aperture_fstop = fstop
    c.dof.focus_distance = (V(*(focus or target)) - V(*loc)).length
    c.dof.aperture_blades = 7
    o = link(bpy.data.objects.new('Cam', c)); o.location = V(*loc)
    o.rotation_euler = (V(*target) - V(*loc)).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.scene.camera = o
    return o


def sweep(color=(0.80, 0.80, 0.80), rough=0.55):
    """Seamless paper: floor curving up into a back wall."""
    prof = [(-2.5, 0.0), (0.45, 0.0)]
    R = 0.45
    for i in range(1, 25):
        a = math.radians(90 * i / 24)
        prof.append((0.45 + R * math.sin(a), R - R * math.cos(a)))
    prof.append((0.9, 2.5))
    bm = bmesh.new()
    rows = [[bm.verts.new((x, y, z)) for (y, z) in prof] for x in (-3, 3)]
    for i in range(len(prof) - 1):
        bm.faces.new((rows[0][i], rows[0][i + 1], rows[1][i + 1], rows[1][i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    o = obj_from_bm(bm, 'sweep', mat_plain('Paper', color, rough), 80)
    return o


def studio(key=4.6, fill=1.2, rim=3.4, top=2.8, hdr_s=0.0, paper=(0.80, 0.80, 0.80)):
    sweep(paper)
    world('studio_small_08_2k.hdr', hdr_s, 40)   # 0: lights only (the HDRI's softbox grid showed up as a weave in the glass)
    area('key', (-420, -520, 560), (0, -10, 25), 0.7, key, spec=0.5)
    area('fill', (520, -420, 260), (0, -10, 25), 0.6, fill, spec=0.4)
    area('rim_l', (-480, 320, 330), (0, 0, 30), 0.25, rim, size_y=0.8)
    area('rim_r', (480, 330, 330), (0, 0, 30), 0.25, rim, size_y=0.8)
    area('top', (0, 60, 700), (0, 0, 20), 0.9, top)


def desk(size=4.0):
    bm = bmesh.new()
    bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=size / 2)
    return obj_from_bm(bm, 'desk', mat_wood())


def keycap_mesh():
    bm = bmesh.new()
    b, t, h = 9.0 * MM, (6.3 * MM, 7.0 * MM), 7.6 * MM
    vb = [bm.verts.new((x, y, 0)) for x, y in ((-b, -b), (b, -b), (b, b), (-b, b))]
    vt = [bm.verts.new((x, y + 0.9 * MM, h)) for x, y in ((-t[0], -t[1]), (t[0], -t[1]), (t[0], t[1]), (-t[0], t[1]))]
    bm.faces.new(vb[::-1]); bm.faces.new(vt)
    for i in range(4):
        bm.faces.new((vb[i], vb[(i + 1) % 4], vt[(i + 1) % 4], vt[i]))
    bmesh.ops.bevel(bm, geom=[e for e in bm.edges if e.verts[0].co.z > 1e-6 or e.verts[1].co.z > 1e-6], offset=1.1 * MM, segments=4, profile=0.5, affect='EDGES')
    me = bpy.data.meshes.new('keycap'); bm.to_mesh(me); bm.free(); smooth(me, 40)
    return me


def keyboard(M, glow=1.0):
    """A black mechanical keyboard with warm backlit legends. Frame = the dock's frame: back face at y=0,
    the dock's base sits on the rear deck (top at z=24.3); the F row starts in front of it."""
    root = empty('keyboard', M)
    case_m = mat_plain('KbCase', (0.014, 0.014, 0.015), 0.5)
    cap_m, nt, b = new_mat('Keycap')
    setp(b, base=(0.012, 0.012, 0.013), rough=0.36)
    N, L = nt.nodes, nt.links
    tc = N.new('ShaderNodeTexCoord'); nz = N.new('ShaderNodeTexNoise'); nz.inputs['Scale'].default_value = 2500
    L.new(tc.outputs['Object'], nz.inputs['Vector'])
    bp = N.new('ShaderNodeBump'); bp.inputs['Strength'].default_value = 0.15; bp.inputs['Distance'].default_value = 0.00004
    L.new(nz.outputs[0], bp.inputs['Height']); L.new(bp.outputs[0], b.inputs['Normal'])
    leg_o = mat_emit('LegendOrange', (1.0, 0.42, 0.10), 3.0 * glow)
    leg_w = mat_emit('LegendWarm', (1.0, 0.80, 0.55), 2.6 * glow)
    x0, x1, yb = -215, 175, -33
    rows_y = [yb - 2.5 - 9.5]
    rows_y.append(rows_y[0] - 24.5)
    for _ in range(4):
        rows_y.append(rows_y[-1] - 19.05)
    yf = rows_y[-1] - 14
    parts = []
    lo = rbox('kb_case', (x1 - x0) * MM, (3 - yf) * MM, 15.5 * MM, 6 * MM, 1.0 * MM, case_m); lo.location = V((x0 + x1) / 2, (3 + yf) / 2, 7.75)
    deck = rbox('kb_deck', (x1 - x0) * MM, (3 - yb) * MM, 10 * MM, 0, 0.8 * MM, case_m); deck.location = V((x0 + x1) / 2, (3 + yb) / 2, DECK / MM - 5)
    wl = rbox('kb_wall_l', 5 * MM, (3 - yf) * MM, 9 * MM, 0, 0.8 * MM, case_m); wl.location = V(x0 + 2.5, (3 + yf) / 2, 19.5)
    wr = rbox('kb_wall_r', 5 * MM, (3 - yf) * MM, 9 * MM, 0, 0.8 * MM, case_m); wr.location = V(x1 - 2.5, (3 + yf) / 2, 19.5)
    wf = rbox('kb_wall_f', (x1 - x0) * MM, 5 * MM, 6 * MM, 0, 0.8 * MM, case_m); wf.location = V((x0 + x1) / 2, yf + 2.5, 18)
    plate = rbox('kb_plate_glow', (x1 - x0 - 10) * MM, (yb - yf - 5) * MM, 0.4 * MM, 0, 0, mat_emit('PlateGlow', (1.0, 0.42, 0.12), 0.08 * glow))
    plate.location = V((x0 + x1) / 2, (yb + yf) / 2, 15.8)
    parts += [lo, deck, wl, wr, wf, plate]
    cap = keycap_mesh(); cap.materials.append(cap_m)
    P = 19.05
    frow = [('Esc', -171.5)] + [('F%d' % (i + 1), x) for i, x in enumerate((-133.4, -114.3, -95.3, -76.2, -47.6, -28.6, -9.5, 9.5, 38.1, 57.2, 76.2, 95.3))] + [('Prt', 124), ('Scr', 143), ('Pau', 162)]
    layout = [frow,
              [(c, -2 + (i - 9) * P) for i, c in enumerate('`1234567890-=')] + [('<-', 64), ('Ins', 124), ('Hm', 143), ('Pg', 162)],
              [('Tab', -202)] + [(c, -11.5 + (i - 7) * P) for i, c in enumerate('QWERTYUIOP[]')] + [('|', 102), ('Del', 124), ('End', 143), ('Pg', 162)],
              [('Cap', -200)] + [(c, -6.7 + (i - 7) * P) for i, c in enumerate("ASDFGHJKL;'")] + [('Ent', 85)],
              [('Sh', -195)] + [(c, 2.8 + (i - 7) * P) for i, c in enumerate('ZXCVBNM,./')] + [('Sh', 92), ('^', 143)],
              [('Ct', -202), ('Wn', -181), ('Alt', -160), ('', -120), ('', -100), ('', -80), ('', -60), ('', -40), ('', -20), ('', 0), ('', 20), ('Alt', 45), ('Fn', 66), ('Ct', 90), ('<', 124), ('v', 143), ('>', 162)]]
    for r, row in enumerate(layout):
        for label, x in row:
            if not (x0 + 12 < x < x1 - 12):
                continue
            k = link(bpy.data.objects.new('key', cap)); k.location = V(x, rows_y[r], 16.6); parts.append(k)
            if label:
                cu = bpy.data.curves.new('legend', 'FONT'); cu.body = label; cu.align_x = 'CENTER'; cu.align_y = 'CENTER'
                cu.size = (4.6 if len(label) < 3 else 3.2) * MM
                cu.materials.append(leg_o if (r < 2 or not label.isalpha()) else leg_w)
                t = link(bpy.data.objects.new('legend', cu)); t.location = V(x, rows_y[r] + 0.9, 16.6 + 7.6 + 0.03); parts.append(t)
    for p in parts:
        parent(p, root)
    return root


def code_image():
    """A made-up dark editor screen for the out-of-focus monitor."""
    w, h = 960, 540
    rng = np.random.default_rng(7)
    px = np.zeros((h, w, 4), np.float32); px[..., 3] = 1; px[..., :3] = (0.035, 0.04, 0.06)
    px[:, :150, :3] = (0.05, 0.055, 0.08); px[h - 26:, :, :3] = (0.07, 0.08, 0.11)
    cols = [(0.45, 0.65, 1.0), (0.95, 0.55, 0.75), (0.6, 0.9, 0.6), (0.9, 0.85, 0.6), (0.75, 0.75, 0.8)]
    y = h - 60
    while y > 30:
        x = 180 + 24 * int(rng.integers(0, 4))
        for _ in range(int(rng.integers(1, 6))):
            ln = int(rng.integers(20, 120))
            if x + ln > w - 40:
                break
            px[y:y + 7, x:x + ln, :3] = cols[int(rng.integers(0, len(cols)))]
            x += ln + 12
        if rng.random() < 0.6:
            px[y:y + 7, 20:20 + int(rng.integers(40, 110)), :3] = (0.4, 0.42, 0.5)
        y -= 16
    im = bpy.data.images.new('code', w, h); im.pixels.foreach_set(px.ravel()); im.pack()
    return im


def monitor(M, strength=2.5, w=620, h=350):
    root = empty('monitor', M)
    black = mat_plain('MonPlastic', (0.012, 0.012, 0.013), 0.4)
    m, nt, b = new_mat('MonScreen')
    tx = nt.nodes.new('ShaderNodeTexImage'); tx.image = code_image()
    nt.links.new(tx.outputs[0], sock(b, ['Emission Color', 'Emission'])); setp(b, base=(0.005, 0.005, 0.006), rough=0.25, emit_s=strength)
    bm = bmesh.new()
    vs = [bm.verts.new(V(x, 0, z)) for x, z in ((-w / 2, 0), (w / 2, 0), (w / 2, h), (-w / 2, h))]
    f = bm.faces.new(vs); uv = bm.loops.layers.uv.new()
    for l, c in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
        l[uv].uv = c
    scr = obj_from_bm(bm, 'mon_screen', m); scr.location = V(0, -7.2, 8)
    body = rbox('mon_body', (w + 14) * MM, 14 * MM, (h + 22) * MM, 0, 2 * MM, black); body.location = V(0, 0, h / 2 + 5)
    neck = rbox('mon_neck', 60 * MM, 30 * MM, 140 * MM, 0, 3 * MM, black); neck.location = V(0, 30, -55)
    foot = rbox('mon_foot', 260 * MM, 190 * MM, 8 * MM, 30 * MM, 2 * MM, black); foot.location = V(0, 10, -121)
    for p in (scr, body, neck, foot):
        parent(p, root)
    return root


def bokeh_lights():
    for i, (x, y, z, c, s) in enumerate(((-260, 900, 190, (1, 0.5, 0.2), 12), (330, 1100, 260, (0.3, 0.6, 1), 10), (120, 1300, 330, (1, 0.8, 0.5), 12),
                                         (-520, 1200, 300, (1, 0.3, 0.5), 8), (520, 800, 120, (1, 0.7, 0.3), 10), (-90, 1500, 420, (0.5, 0.8, 1), 10))):
        bm = bmesh.new(); bmesh.ops.create_uvsphere(bm, u_segments=12, v_segments=8, radius=0.006)
        o = obj_from_bm(bm, 'bokeh%d' % i, mat_emit('Bokeh%d' % i, c, s)); o.location = V(x, y, z)


# ---------------------------------------------------------------- shots
G0 = T(0, 0, -24.3)                         # dock standing on the ground
STUDIO_CABLE = [(82, -1, 27), (104, 14, 9), (122, 55, 1.9), (150, 160, 1.9), (230, 420, 1.9)]
EXPOSURE = {}


def RZ(deg):
    return Matrix.Rotation(math.radians(deg), 4, 'Z')


def studio_dock(turn, screen, cable=STUDIO_CABLE, **kw):
    """Dock on the sweep, turned about Z (0 = screen to the camera); the lights and camera stay put."""
    studio()
    d = Dock(RZ(turn) @ G0, screen, 2.0, R=RZ(turn), **kw)
    if cable:
        d.cable(cable)
    return d


def shot_front():
    studio_dock(0, 'page_date.png')
    camera((0, -430, 70), (5, -10, 24), 150, 11, focus=(0, -12, 32))


def shot_three_quarter():
    studio_dock(-33, 'mood_1_happy.png')
    camera((0, -430, 150), (3, -6, 22), 150, 11, focus=(0, -12, 32))


def shot_side():
    studio_dock(90, 'page_clock.png')
    camera((0, -430, 45), (-6, 0, 23), 150, 11)


def shot_back():
    studio_dock(152, 'page_clock.png', [(84, -1, 28), (108, -16, 10), (128, -60, 1.9), (150, -160, 1.9), (200, -420, 1.9)])
    camera((0, -430, 150), (-8, 2, 22), 150, 11)


def shot_exploded():
    d = studio_dock(-28, 'mood_6_excited.png', [(112, -8, 104), (150, 0, 80), (180, 30, 30), (196, 90, 1.9), (260, 400, 1.9)],
                    head_off=(0, 0, 28), board_off=(0, -6, 42), plug_off=(24, 0, 0))
    camera((0, -640, 250), (24, -10, 57), 122, 13, focus=(0, -25, 60))


def shot_flat():
    """The dock empty, the display unit lying beside it, still plugged in and running."""
    studio()
    Dock(T(-38, 8, 0) @ RZ(-22) @ G0, board=False, plug=False)
    Mb = T(36, -36, BT / 2 / MM + 0.05) @ RZ(-12)
    make_board('page_moon.png', 2.0, Mb)
    pl, white, ex = make_plug(Mb)
    cable_from(Mb, ex, white, [(112, -56, 2.0), (142, -38, 1.9), (162, 40, 1.9), (200, 400, 1.9)])
    camera((-30, -440, 270), (8, -12, 12), 92, 13, focus=(10, -30, 12))


def on_ground(o, M, x, y):
    """Apply M to object o, then slide it so its footprint is centred on (x, y) mm."""
    o.matrix_world = M
    bpy.context.view_layer.update()
    cs = [o.matrix_world @ Vector(c) for c in o.bound_box]
    cx = (min(c.x for c in cs) + max(c.x for c in cs)) / 2; cy = (min(c.y for c in cs) + max(c.y for c in cs)) / 2
    o.matrix_world = Matrix.Translation((x * MM - cx, y * MM - cy, -min(c.z for c in cs))) @ o.matrix_world


def shot_flatlay():
    """Top-down: the two printed parts, the board and the cable on a wood desk."""
    desk(); world('brown_photostudio_02_2k.hdr', 0.25, 120)
    area('key', (-500, -300, 700), (0, 0, 0), 0.8, 13)
    area('fill', (500, 300, 500), (0, 0, 0), 0.6, 2)
    base = import_stl('dock_base_v8.stl', mat_pla(2)); on_ground(base, Matrix.Identity(4), -50, 36)
    head = import_stl('dock_head_v8.stl', mat_pla(0))
    on_ground(head, rest_matrix(head, (B.to_3x3() @ Vector((0, 0, -1)))), -50, -22)
    Mb = T(34, 30, BT / 2 / MM + 0.05) @ RZ(5)
    make_board('react_ci_green.png', 1.8, Mb)
    pl, white, ex = make_plug(T(10, -28, 2.75 + 2.55 + 0.05) @ RZ(180) @ T(-BW / 2 / MM - 2, 0, 0))
    cable_from(pl.matrix_world, ex, white, [(-28, -56, 1.9), (10, -74, 1.9), (62, -62, 1.9), (88, -16, 1.9), (96, 50, 1.9), (120, 300, 1.9)])
    camera((0, 0, 520), (0, 0, 0), 100, 11)


def hero_set(glow=1.0, hdr=0.14):
    desk(); world('brown_photostudio_02_2k.hdr', hdr, 200)
    keyboard(Matrix.Identity(4), glow)
    monitor(T(-120, 520, 110) @ RZ(-8), 0.6)
    bokeh_lights()


HERO_CABLE = [(86, -1, 53), (112, 10, 44), (132, 40, 12), (150, 120, 1.9), (240, 420, 1.9)]


def shot_hero():
    hero_set()
    area('key', (-380, -250, 520), (0, -10, 50), 0.6, 3.2, (1.0, 0.95, 0.9))
    area('rim', (350, 350, 330), (0, 0, 50), 0.4, 2.4, (0.8, 0.88, 1.0))
    d = Dock(Matrix.Identity(4), 'page_weather.png', 1.5); d.cable(HERO_CABLE)
    camera((14, -190, 150), (4, -28, 42), 50, 4, focus=(0, -8, 56))


def shot_closeup():
    hero_set(hdr=0.12)
    area('key', (-380, -250, 520), (0, -10, 50), 0.6, 3.0, (1.0, 0.95, 0.9))
    area('rim', (350, 350, 330), (0, 0, 50), 0.4, 2.4, (0.8, 0.88, 1.0))
    d = Dock(Matrix.Identity(4), 'status_claude_done_0.png', 1.4); d.cable(HERO_CABLE)
    camera((-45, -128, 100), (-1, -6, 54), 85, 8, focus=(0, -8, 57))
    EXPOSURE['screen-closeup'] = 0.8


def shot_night():
    """On the desk in front of the monitor at night."""
    desk(); world('brown_photostudio_02_2k.hdr', 0.012, 200)
    monitor(T(-120, 270, 72) @ RZ(6), 0.5)
    keyboard(T(-265, -105, 0) @ RZ(4), 1.2)
    area('lamp', (520, -300, 420), (0, 0, 20), 0.5, 2.2, (1.0, 0.62, 0.32))
    d = Dock(G0, 'status_focus_0.png', 0.8); d.cable(STUDIO_CABLE)
    camera((62, -245, 72), (-12, 0, 46), 50, 2.8, focus=(0, -8, 32))
    EXPOSURE['night'] = 1.8


# ---------------------------------------------------------------- clips (B-roll)
def ease(x):
    x = min(1.0, max(0.0, x))
    return x * x * (3 - 2 * x)


def seg(t, a, b):
    return ease((t - a) / (b - a))


def lerp(a, b, s):
    return tuple(p + (q - p) * s for p, q in zip(a, b))


def aim(cam, loc, target, focus=None):
    cam.location = V(*loc)
    cam.rotation_euler = (V(*target) - V(*loc)).to_track_quat('-Z', 'Y').to_euler()
    cam.data.dof.focus_distance = (V(*(focus or target)) - V(*loc)).length


def drop(o):
    data = o.data
    bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.curves.remove(data)


def clip_turntable():
    """Slow full turn on the studio sweep (loops)."""
    studio()
    d = Dock(G0, 'mood_1_happy.png', 2.0, plug=False)
    camera((0, -470, 150), (0, 0, 24), 150, 11, focus=(0, -14, 34))

    def up(f, n):
        a = 360.0 * f / n - 30
        d.pose(G=RZ(a) @ T(0, 15, 0) @ G0, R=RZ(a))
    return up, 0.0


def clip_dolly():
    """Slow push-in on the hero set."""
    hero_set()
    area('key', (-380, -250, 520), (0, -10, 50), 0.6, 3.2, (1.0, 0.95, 0.9))
    area('rim', (350, 350, 330), (0, 0, 50), 0.4, 2.4, (0.8, 0.88, 1.0))
    d = Dock(Matrix.Identity(4), 'page_weather.png', 1.5); d.cable(HERO_CABLE)
    cam = camera((14, -190, 150), (4, -28, 42), 50, 4)

    def up(f, n):
        s = ease(f / (n - 1))
        aim(cam, lerp((34, -285, 196), (10, -165, 128), s), lerp((6, -40, 34), (3, -20, 47), s), focus=(0, -8, 56))
    return up, 0.0


def clip_rack():
    """Rack focus from the backlit keys to the screen."""
    hero_set(hdr=0.12)
    area('key', (-380, -250, 520), (0, -10, 50), 0.6, 3.0, (1.0, 0.95, 0.9))
    area('rim', (350, 350, 330), (0, 0, 50), 0.4, 2.4, (0.8, 0.88, 1.0))
    d = Dock(Matrix.Identity(4), 'status_claude_done_0.png', 1.4); d.cable(HERO_CABLE)
    cam = camera((30, -200, 40), (2, -20, 46), 50, 2.8)

    def up(f, n):
        t = f / (n - 1)
        s = seg(t, 0.22, 0.68)
        aim(cam, lerp((34, -200, 40), (26, -196, 42), t), (2, -20, 41), focus=lerp((8, -106, 25), (0, -8, 57), s))
    return up, 0.3


def clip_exploded():
    """Comes apart, holds, goes back together."""
    studio()
    turn = -28
    d = Dock(RZ(turn) @ G0, 'mood_6_excited.png', 2.0, R=RZ(turn))
    cam = camera((0, -640, 250), (20, -10, 50), 100, 13, focus=(0, -25, 60))
    apart = [(112, -8, 104), (150, 0, 80), (180, 30, 30), (196, 90, 1.9), (260, 400, 1.9)]
    state = {}

    def up(f, n):
        t = f / (n - 1)
        ep = seg(t, 0.04, 0.20) * (1 - seg(t, 0.80, 0.94))
        eb = seg(t, 0.12, 0.32) * (1 - seg(t, 0.66, 0.84))
        eh = seg(t, 0.20, 0.40) * (1 - seg(t, 0.58, 0.76))
        d.pose((0, 0, 28 * eh), (0, -6 * eb, 42 * eb), (24 * ep, 0, 0))
        if 'cable' in state:
            drop(state['cable'])
        w = (28 * eh + 42 * eb) / 70.0
        state['cable'] = d.cable([lerp(p, q, w) for p, q in zip(STUDIO_CABLE, apart)])
        aim(cam, (0, -640, 250), (20, -10, 34 + 20 * max(eh, eb)), focus=(0, -25, 60))
    return up, 0.0


def clip_night():
    """Night desk: slow slide, the screen's cursor blinking."""
    desk(); world('brown_photostudio_02_2k.hdr', 0.012, 200)
    monitor(T(-120, 270, 72) @ RZ(6), 0.5)
    keyboard(T(-265, -105, 0) @ RZ(4), 1.2)
    area('lamp', (520, -300, 420), (0, 0, 20), 0.5, 2.2, (1.0, 0.62, 0.32))
    d = Dock(G0, 'status_lockedin_0.png', 0.8); d.cable(STUDIO_CABLE)
    tex = mat_screen.tex
    frames = [img(os.path.join(SHOTS_DIR, 'status_lockedin_%d.png' % i)) for i in (0, 2)]
    cam = camera((62, -245, 72), (-12, 0, 46), 50, 2.8, focus=(0, -8, 32))

    def up(f, n):
        s = ease(f / (n - 1))
        tex.image = frames[(f // 15) % 2]
        aim(cam, lerp((96, -252, 66), (44, -232, 78), s), lerp((-8, 0, 31), (-16, 0, 33), s), focus=(0, -8, 32))
    return up, 1.8


CLIPS = {'turntable': (clip_turntable, 8), 'dolly-in': (clip_dolly, 6), 'rack-focus': (clip_rack, 6),
         'exploded': (clip_exploded, 8), 'night': (clip_night, 6)}


def run_clip(name):
    """Renders the clip as numbered JPEG frames in OUT/<name>/ (skips frames that exist); encode.sh makes the MP4."""
    build, seconds = CLIPS[name]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    setup_render()
    sc = bpy.context.scene
    sc.render.use_persistent_data = True
    up, exposure = build()
    sc.view_settings.exposure = exposure
    n = seconds * FPS
    os.makedirs(os.path.join(OUT, name), exist_ok=True)
    for f in range(0, n, STEP):
        path = os.path.join(OUT, name, '%04d.jpg' % f)
        if os.path.exists(path):
            continue
        up(f, n)
        bpy.context.view_layer.update()
        sc.render.filepath = path
        bpy.ops.render.render(write_still=True)
        print('FRAME', name, f, n, flush=True)
    print('CLIPDONE', name, flush=True)


SHOTS = {'hero': shot_hero, 'front': shot_front, 'three-quarter': shot_three_quarter, 'side': shot_side, 'back': shot_back,
         'exploded': shot_exploded, 'out-of-dock': shot_flat, 'flat-lay': shot_flatlay, 'night': shot_night, 'screen-closeup': shot_closeup}
ORDER = list(SHOTS)


def setup_render():
    sc = bpy.context.scene
    sc.render.engine = 'CYCLES'
    try:
        prefs = bpy.context.preferences.addons['cycles'].preferences
        for kind in ('OPTIX', 'CUDA'):
            try:
                prefs.compute_device_type = kind; prefs.get_devices()
                devs = [dv for dv in prefs.devices if dv.type == kind]
                if devs:
                    for dv in prefs.devices:
                        dv.use = dv.type == kind
                    sc.cycles.device = 'GPU'
                    print('GPU:', kind, [dv.name for dv in devs])
                    break
            except Exception as e:
                print('no', kind, e)
    except Exception as e:
        print('GPU setup failed, CPU:', e)
    sc.cycles.samples = SAMPLES; sc.cycles.use_adaptive_sampling = True; sc.cycles.adaptive_threshold = 0.01 if SAMPLES < 1000 else 0.004
    sc.cycles.use_denoising = True
    try:
        sc.cycles.denoiser = 'OPENIMAGEDENOISE'; sc.cycles.denoising_input_passes = 'RGB_ALBEDO_NORMAL'
    except Exception as e:
        print('denoiser:', e)
    sc.cycles.max_bounces = 8; sc.cycles.sample_clamp_indirect = 8
    sc.render.resolution_x, sc.render.resolution_y = RES; sc.render.resolution_percentage = 100
    sc.view_settings.view_transform = 'AgX'
    try:
        sc.view_settings.look = 'AgX - Medium High Contrast'
    except Exception:
        try:
            sc.view_settings.look = 'Medium High Contrast'
        except Exception as e:
            print('look:', e)
    print('LOOK', sc.view_settings.look, flush=True)
    sc.render.image_settings.file_format = 'JPEG'; sc.render.image_settings.quality = 97
    sc.unit_settings.scale_length = 1.0


def main():
    if sys.platform == 'win32':
        import ctypes
        ctypes.windll.kernel32.SetThreadExecutionState(0x80000001)   # keep the PC awake while rendering
    if CLIPS_ARG:
        for name in CLIPS_ARG.split(','):
            run_clip(name)
        return
    names = arg('--shots', ','.join(ORDER)).split(',')
    os.makedirs(OUT, exist_ok=True)
    for name in names:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        setup_render()
        SHOTS[name]()
        sc = bpy.context.scene
        sc.view_settings.exposure = EXPOSURE.get(name, 0.0)
        sc.render.filepath = os.path.join(OUT, 'product-%02d-%s%s.jpg' % (ORDER.index(name) + 1, name, SUFFIX))
        if BLEND and name == names[0]:
            bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(BLEND), relative_remap=True)
        bpy.ops.render.render(write_still=True)
        print('DONE', sc.render.filepath, flush=True)


main()
