"""The Loft's surface pictures, made with Blender's Python module (a build tool).

    python3 tools/blender/loft_textures.py [out_dir] [surface ...]

Needs `pip install bpy`. Writes, for each surface, a flat tile (.glb) that carries
its two pictures: the colour (base) and the roughness (green; blue is 0, no metal).
Every picture is made by text recipes with fixed seeds: the same files every run,
and every one tiles without a seam (all the smoothing wraps round).

The engine has no normal maps yet, so the relief is painted into the picture:
the slopes of each surface's own height are lit from the upper left, and its
hollows are darkened. The roughness picture is where paint is glossy (crests),
dull (pits, dirt) or worn (scuffs).

  block   painted concrete block, 2.4 x 2.0 m: 40 x 20 cm blocks in running bond,
          1 cm painted joints, orange-peel paint, pits, voids
  floor   oak boards 15 cm wide, 2.4 x 2.4 m: staggered joints, grain, rings, knots
  plate   painted diamond plate, 0.64 x 0.64 m: 20 x 20 raised lozenges, dust in the valleys
  deck    corrugated steel deck, 1.2 x 1.2 m: 15 cm flutes, a few dents
"""
import math
import os
import sys

import bpy  # first: it makes bmesh and the rest importable
import bmesh
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "projects", "loft", "models")


# --------------------------------------------------------------- maths helpers
def blur(a, sy, sx=None):
    """Gaussian blur by FFT (sigmas in pixels): wraps round, so what it makes tiles."""
    sx = sy if sx is None else sx
    h, w = a.shape
    fy = np.fft.fftfreq(h)[:, None]
    fx = np.fft.rfftfreq(w)[None, :]
    g = np.exp(-2 * np.pi ** 2 * ((sy * fy) ** 2 + (sx * fx) ** 2))
    return np.fft.irfft2(np.fft.rfft2(a) * g, s=a.shape)


def noise(shape, rng, sy, sx=None):
    n = blur(rng.standard_normal(shape), sy, sx)
    return n / (n.std() + 1e-9)


def smooth(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def splat(field, XX, YY, cx, cy, r, depth, pix):
    """A round dent into `field` (max of what is there)."""
    h, w = field.shape
    x0, x1 = int(max((cx - 2.2 * r) / pix, 0)), int(min((cx + 2.2 * r) / pix, w - 1))
    y0, y1 = int(max((cy - 2.2 * r) / pix, 0)), int(min((cy + 2.2 * r) / pix, h - 1))
    if x1 <= x0 or y1 <= y0:
        return
    d2 = ((XX[y0:y1, x0:x1] - cx) ** 2 + (YY[y0:y1, x0:x1] - cy) ** 2) / (r * r)
    field[y0:y1, x0:x1] = np.maximum(field[y0:y1, x0:x1], depth * np.exp(-d2 * 1.6))


# ------------------------------------------------------------------ surfaces
def block_paint():
    """Painted concrete block, 2.4 m wide x 2.0 m high: 6 blocks x 10 courses (running bond)."""
    W, H = 2.4, 2.0
    TW, TH = 1920, 1600
    pix = W / TW  # 1.25 mm
    rng = np.random.default_rng(11)
    X = (np.arange(TW) + 0.5) * pix
    Y = (np.arange(TH) + 0.5) * pix  # row 0 is the bottom
    XX, YY = np.meshgrid(X, Y)
    sp = lambda m: m / pix

    j = np.floor(YY / 0.2).astype(int)
    ly = YY - j * 0.2
    odd = (j % 2) == 1
    xs = XX - np.where(odd, 0.2, 0.0)
    bi = np.floor(xs / 0.4).astype(int)
    lx = xs - bi * 0.4
    jy = np.minimum(ly, 0.2 - ly)  # distance to the joint in a course
    jx = np.minimum(lx, 0.4 - lx)
    joint_d = np.minimum(jy, jx)  # distance to the nearest joint
    joint = joint_d < 0.005

    # the height of the paint: block faces wobble slowly, the joints sit 1.5 mm lower, and the
    # paint itself is stippled (blobs of 1 to 3 cm with finer ones on top: orange peel)
    stipple = 1.0 * noise((TH, TW), rng, sp(0.014)) + 0.5 * noise((TH, TW), rng, sp(0.006)) + 0.18 * noise((TH, TW), rng, sp(0.0025))
    wobble = noise((TH, TW), rng, sp(0.05))
    chamfer = smooth(0.0, 0.0075, joint_d)  # 0 at the joint's middle, 1 on the face
    h = 0.9e-3 * stipple + 0.6e-3 * wobble - 1.8e-3 * (1 - chamfer)

    # pits and voids: craters, more along joints (where the block's edge chipped)
    pits = np.zeros((TH, TW))
    for _ in range(1400):
        splat(pits, XX, YY, rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0.0012, 0.0032), rng.uniform(0.3, 0.8), pix)
    for _ in range(230):
        row = rng.integers(0, 10)
        splat(pits, XX, YY, rng.uniform(0, W), row * 0.2 + rng.choice([0.011, 0.189]), rng.uniform(0.002, 0.0045), rng.uniform(0.5, 0.9), pix)
    voids = np.zeros((TH, TW))
    for _ in range(5):
        splat(voids, XX, YY, rng.uniform(0.1, W - 0.1), rng.uniform(0.1, H - 0.1), rng.uniform(0.005, 0.011), 1.0, pix)
    h -= 1.2e-3 * pits + 3e-3 * voids

    # baked relief: h is in metres, a pixel is 1.25 mm, so h / pix is the slope per pixel
    gy, gx = np.gradient(h / pix)
    slope = -(gx * -0.6 + gy * 0.8)
    cav = blur(h, 7) - h
    lit = np.clip(1.0 + 2.6 * slope - 0.45 * np.clip(cav / 0.0010, 0, 0.5), 0.6, 1.4)

    # per block tone, and a slow stain, and drips
    tone = np.zeros((TH, TW))
    warm = np.zeros((TH, TW))
    for jj in range(-1, 11):
        for bb in range(-1, 7):
            m = (j == jj) & (bi == bb)
            if m.any():
                tone[m] = rng.normal(0, 1)
                warm[m] = rng.normal(0, 1)
    paint = np.array([0.76, 0.74, 0.69])  # warm light grey, sRGB
    col = paint[None, None, :] * (1 + 0.016 * tone)[:, :, None]
    col[:, :, 0] *= 1 + 0.008 * warm
    col[:, :, 2] *= 1 - 0.010 * warm
    col *= lit[:, :, None]
    col *= (1 + 0.03 * noise((TH, TW), rng, sp(0.30)))[:, :, None]
    col *= (1 + 0.02 * noise((TH, TW), rng, sp(0.004), sp(0.5)))[:, :, None]  # vertical drips
    col[joint] *= 0.95
    col *= (1 - 0.80 * np.clip(pits, 0, 1))[:, :, None]
    col = col * (1 - voids[:, :, None]) + np.array([0.09, 0.085, 0.08])[None, None, :] * voids[:, :, None]

    # roughness: glossy paint on the crests, dull in pits, voids and joints
    crest = np.clip(0.5 + 0.5 * stipple / 2.0, 0, 1)
    rough = 0.52 - 0.14 * crest + 0.34 * np.clip(pits, 0, 1) + 0.30 * voids + 0.06 * joint
    rough += 0.03 * noise((TH, TW), rng, sp(0.02))
    return col, np.clip(rough, 0.2, 1.0)


def oak_floor():
    """Oak boards 15 cm wide: the tile is 2.4 m along the boards (u) and 2.4 m across (16 boards)."""
    TW = TH = 2048
    pix = 2.4 / TW
    rng = np.random.default_rng(21)
    X = (np.arange(TW) + 0.5) * pix  # along the boards
    Y = (np.arange(TH) + 0.5) * pix  # across them
    XX, YY = np.meshgrid(X, Y)
    sp = lambda m: m / pix
    board = np.floor(YY / 0.15).astype(int)
    by = YY - board * 0.15

    # each board: one length, or two (a butt joint at a random place), its own tone and grain offset
    seg = np.zeros((TH, TW), dtype=int)
    gapx = np.full((TH, TW), 1.0)
    for b in range(16):
        rows = board == b
        if rng.random() < 0.55:
            cut = rng.uniform(0.7, 1.7)
            seg[rows & (XX > cut)] = 1
            gapx[rows] = np.minimum(np.abs(XX[rows] - cut), np.minimum(XX[rows], 2.4 - XX[rows]))
        else:
            gapx[rows] = np.minimum(XX[rows], 2.4 - XX[rows])
    edge = np.minimum(np.minimum(by, 0.15 - by), gapx)  # distance to the board's edge
    gap = edge < 0.0011
    bev = smooth(0.0011, 0.0038, edge)  # 0 in the gap, 1 on the board's face

    ids = board * 2 + seg
    tone = np.zeros((TH, TW))
    warmth = np.zeros((TH, TW))
    shift = np.zeros((TH, TW))
    for k in np.unique(ids):
        m = ids == k
        tone[m] = rng.normal(0, 1)
        warmth[m] = rng.normal(0, 1)
        shift[m] = rng.uniform(0, 50)

    grain = noise((TH, TW), rng, 1.3, 90)       # long streaks
    fine = noise((TH, TW), rng, 0.7, 22)        # pores
    low = noise((TH, TW), rng, sp(0.09), sp(0.5))
    # growth rings: bands along the board, bent by the slow noise (cathedral figure)
    ring_v = by / 0.0042 + 2.4 * low + shift
    rings = 0.5 + 0.5 * np.sin(ring_v)
    rings = np.where(rings > 0.5, rings, rings * 0.8)
    # a few knots: concentric rings round a point
    knot = np.zeros((TH, TW))
    for _ in range(5):
        kx, ky = rng.uniform(0, 2.4), rng.uniform(0, 2.4)
        r = np.sqrt(((XX - kx) / 1.9) ** 2 + (YY - ky) ** 2)
        knot += np.exp(-(r / 0.018) ** 2) * (0.6 + 0.4 * np.sin(r / 0.0022))
    t = np.clip(0.50 + 0.17 * grain + 0.07 * fine + 0.20 * (rings - 0.5) + 0.07 * low - 0.35 * np.clip(knot, 0, 1) + 0.07 * tone, 0, 1)
    light = np.array([0.74, 0.55, 0.34])  # sRGB, like a pale oak
    dark = np.array([0.40, 0.26, 0.14])
    col = dark[None, None, :] * (1 - t[:, :, None]) + light[None, None, :] * t[:, :, None]
    col[:, :, 0] *= 1 + 0.035 * warmth
    col[:, :, 2] *= 1 - 0.05 * warmth
    # the softened edge catches the light on its upper-left sides and is dirtier in the gaps
    ey, ex = np.gradient(bev)
    col *= (1 + 7.5 * (-(ex * -0.6 + ey * 0.8)) * 1.0)[:, :, None].clip(0.5, 1.5)
    col *= (0.55 + 0.45 * bev)[:, :, None]
    col[gap] = np.array([0.045, 0.035, 0.03])
    # wear: paler, rougher streaks where feet go
    wear = np.clip(noise((TH, TW), rng, sp(0.6), sp(0.12)) * 0.5, 0, 1)
    col *= (1 + 0.07 * wear)[:, :, None]
    rough = 0.46 + 0.12 * fine * 0.5 + 0.18 * wear + 0.04 * grain + 0.35 * (1 - bev)
    rough[gap] = 0.95
    return col, np.clip(rough, 0.25, 1.0)


def diamond_plate():
    """Painted diamond plate, 0.64 m: 20 x 20 lozenges, neighbours at right angles."""
    T = 2048
    L = 0.64
    pix = L / T
    rng = np.random.default_rng(31)
    P = L / 20
    X = (np.arange(T) + 0.5) * pix
    XX, YY = np.meshgrid(X, X)
    i = np.floor(XX / P).astype(int)
    k = np.floor(YY / P).astype(int)
    dx = XX - (i + 0.5) * P
    dy = YY - (k + 0.5) * P
    ang = np.where((i + k) % 2 == 0, math.radians(45), math.radians(-45))
    u = dx * np.cos(ang) + dy * np.sin(ang)
    w = -dx * np.sin(ang) + dy * np.cos(ang)
    a, b = 0.0148, 0.0042
    r = np.sqrt((u / a) ** 2 + (w / b) ** 2)
    lozenge = smooth(1.0, 0.45, r)  # 1 on the flat top, 0 outside
    h = 2.6e-3 * lozenge
    # paint: a skin over everything, so the ridges are a touch soft; sags and brush texture
    h += 0.12e-3 * noise((T, T), rng, 3) + 0.25e-3 * noise((T, T), rng, 40)
    gy, gx = np.gradient(h / pix)
    slope = -(gx * -0.6 + gy * 0.8)
    cav = blur(h, 14) - h
    lit = np.clip(1.0 + 0.55 * slope - 0.9 * np.clip(cav / 0.0012, 0, 0.5), 0.5, 1.5)
    paint = np.array([0.81, 0.78, 0.70])  # cream
    col = paint[None, None, :] * lit[:, :, None]
    dust = np.clip(0.5 + 0.5 * noise((T, T), rng, 25), 0, 1)
    valley = 1 - lozenge
    col *= (1 - 0.10 * valley * dust)[:, :, None]  # dust in the valleys
    col *= (1 + 0.025 * noise((T, T), rng, 120))[:, :, None]
    # scuffs: dark rubber and grey metal showing where shoes scraped the crests
    scuff = np.zeros((T, T))
    for _ in range(70):
        cx, cy = rng.uniform(0, L), rng.uniform(0, L)
        ln, an = rng.uniform(0.02, 0.07), rng.uniform(0, math.pi)
        for s in np.linspace(-ln, ln, 14):
            splat(scuff, XX, YY, cx + s * math.cos(an), cy + s * math.sin(an), rng.uniform(0.0006, 0.0016), rng.uniform(0.3, 0.9), pix)
    scuff *= lozenge
    col = col * (1 - 0.35 * scuff[:, :, None]) + np.array([0.30, 0.30, 0.31])[None, None, :] * 0.35 * scuff[:, :, None] * 0.5
    # specks of grit and a few dark hairs of dirt
    for _ in range(260):
        splat(scuff, XX, YY, rng.uniform(0, L), rng.uniform(0, L), rng.uniform(0.0004, 0.0012), 1.0, pix)
    col *= (1 - 0.45 * np.clip(scuff, 0, 1) * (1 - lozenge))[:, :, None]
    rough = 0.50 - 0.10 * lozenge + 0.22 * (1 - lozenge) * dust + 0.30 * np.clip(scuff, 0, 1)
    return col, np.clip(rough, 0.25, 1.0)


def deck():
    """Corrugated steel deck, 1.2 m square: flutes along u, 15 cm pitch, trapezoid, a few dents."""
    T = 2048
    L = 1.2
    pix = L / T
    rng = np.random.default_rng(41)
    X = (np.arange(T) + 0.5) * pix
    XX, YY = np.meshgrid(X, X)
    pitch = 0.15
    v = (YY % pitch) / pitch  # across a flute, 0..1
    # bottom flat 0.04, up 0.03, rib 0.05, down 0.03 (of 0.15)
    # (flat bottom, slope up, rib, slope down): the corners are rounded by a blur
    prof = np.zeros_like(v)
    prof = np.where(v < 0.2, 0.0, prof)
    prof = np.where((v >= 0.2) & (v < 0.4), (v - 0.2) / 0.2, prof)
    prof = np.where((v >= 0.4) & (v < 0.733), 1.0, prof)
    prof = np.where((v >= 0.733) & (v < 0.933), 1 - (v - 0.733) / 0.2, prof)
    prof = np.where(v >= 0.933, 0.0, prof)
    h = 38e-3 * blur(prof, 5, 5)
    # dents: soft hollows with a crease, in the flats and the ribs alike
    dent = np.zeros((T, T))
    for _ in range(9):
        splat(dent, XX, YY, rng.uniform(0, L), rng.uniform(0, L), rng.uniform(0.03, 0.08), rng.uniform(0.5, 1.0), pix)
    dent = blur(dent, 30)
    dent = dent / (dent.max() + 1e-9)
    h -= 3.5e-3 * dent
    h += 0.15e-3 * noise((T, T), rng, 2.5) + 0.1e-3 * noise((T, T), rng, 300)
    gy, gx = np.gradient(h / pix)
    slope = -(gx * -0.2 + gy * 0.97)  # lit mostly across the flutes
    cav = blur(h, 40) - h
    lit = np.clip(1.0 + 0.95 * slope - 0.7 * np.clip(cav / 0.012, 0, 0.5), 0.45, 1.5)
    paint = np.array([0.81, 0.79, 0.73])
    col = paint[None, None, :] * lit[:, :, None]
    col *= (1 + 0.035 * noise((T, T), rng, 160, 400))[:, :, None]
    col *= (1 - 0.05 * np.clip(noise((T, T), rng, 25), 0, 3))[:, :, None]
    rough = 0.48 + 0.12 * (1 - np.clip(prof * 2, 0, 1)) + 0.05 * noise((T, T), rng, 9)
    # the dents' creases have chipped paint: a touch of bare metal
    rough += 0.08 * dent
    return col, np.clip(rough, 0.25, 1.0)


# -------------------------------------------------------------- packaging
def make_image(name, rgb, path, noncolor=False):
    h, w, _ = rgb.shape
    img = bpy.data.images.new(name, w, h, alpha=False)
    px = np.ones((h, w, 4), dtype=np.float32)
    px[:, :, :3] = np.clip(rgb, 0, 1)
    img.pixels.foreach_set(px.ravel())
    img.file_format = "JPEG"
    img.filepath_raw = path
    img.save()
    img.colorspace_settings.name = "Non-Color" if noncolor else "sRGB"
    img.pack()
    return img


def tile_glb(name, col, rough, size, repeat, out_dir):
    """A flat tile `size` (w, h) metres, facing +z, its pictures repeating `repeat` times across it."""
    base = make_image(name + "_color", col, os.path.join(out_dir, name + "_color.jpg"))
    rr = np.stack([np.ones_like(rough), rough, np.zeros_like(rough)], axis=2)
    mr = make_image(name + "_rough", rr, os.path.join(out_dir, name + "_rough.jpg"), noncolor=True)
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    t1 = nt.nodes.new("ShaderNodeTexImage")
    t1.image = base
    nt.links.new(t1.outputs["Color"], bsdf.inputs["Base Color"])
    t2 = nt.nodes.new("ShaderNodeTexImage")
    t2.image = mr
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(t2.outputs["Color"], sep.inputs["Color"])
    nt.links.new(sep.outputs["Green"], bsdf.inputs["Roughness"])
    nt.links.new(sep.outputs["Blue"], bsdf.inputs["Metallic"])
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new()
    w, h = size
    # glTF axes (x right, y up, z to the viewer) become Blender's (x, -z, y)
    pts = [(-w / 2, 0, 0), (w / 2, 0, 0), (w / 2, h, 0), (-w / 2, h, 0)]
    uvs = [(0, 0), (repeat[0], 0), (repeat[0], repeat[1]), (0, repeat[1])]
    vs = [bm.verts.new((x, -z, y)) for (x, y, z) in pts]
    f = bm.faces.new(vs)
    for l, c in zip(f.loops, uvs):
        l[uv].uv = c
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(mat)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    path = os.path.join(out_dir, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", use_selection=True, export_yup=True, export_apply=True,
                              export_image_format="JPEG", export_jpeg_quality=90, export_materials="EXPORT",
                              export_cameras=False, export_lights=False)
    bpy.data.objects.remove(ob)
    os.remove(os.path.join(out_dir, name + "_color.jpg"))
    os.remove(os.path.join(out_dir, name + "_rough.jpg"))
    print(f"{name}: {os.path.getsize(path) / 1e6:.2f} MB")


SURFACES = {
    # name: (recipe, size of one carrier tile in metres, how often the picture repeats across it)
    "block": (block_paint, "loft_block_tile", (2.4, 2.0), (1, 1)),
    "floor": (oak_floor, "loft_floor_tile", (2.4, 2.4), (1, 1)),
    "plate": (diamond_plate, "loft_plate_tile", (0.64, 0.64), (1, 1)),
    "deck": (deck, "loft_deck_tile", (2.4, 2.4), (2, 2)),
}

if __name__ == "__main__":
    args = sys.argv[1:]
    out = os.path.abspath(args[0]) if args and args[0] not in SURFACES else OUT
    want = [a for a in args if a in SURFACES] or list(SURFACES)
    os.makedirs(out, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for s in want:
        recipe, name, size, rep = SURFACES[s]
        col, rough = recipe()
        tile_glb(name, col, rough, size, rep, out)
