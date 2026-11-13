#!/usr/bin/env python3
"""Brings the bar from the original repo (Barcode162/3D) into LOOP3D.

    tools/import_bar.py <3D repo folder>      → projects/bar3d/

Reads the bar's world files (assets/world/places/bar.json and kinds/bar.json:
every thing, its kind, where it stands, its lights, walls and raised floors)
and writes projects/bar3d/bar.loop in Looped Script, with the models it uses
copied to projects/bar3d/models/ and their credits. Run it again whenever the
bar changes there; it never edits the 3D repo.
"""
import json
import math
import shutil
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent
src = Path(sys.argv[1] if len(sys.argv) > 1 else "/home/user/barcode162/3d")
world = src / "assets" / "world"
out = root / "projects" / "bar3d"
(out / "models").mkdir(parents=True, exist_ok=True)

place = json.loads((world / "places" / "bar.json").read_text())
kinds = {k["id"]: k for k in json.loads((world / "kinds" / "bar.json").read_text())["kinds"]}
things = {t["id"]: t for t in place["things"]}


def part(thing, name):
    """A part from the thing itself, else its kind, else what its kind builds on."""
    if name in thing.get("parts", {}):
        return thing["parts"][name]
    k = kinds.get(thing["kind"])
    seen = set()
    while k and k["id"] not in seen:
        seen.add(k["id"])
        if name in k.get("parts", {}):
            return k["parts"][name]
        k = kinds.get(k.get("is"))
    return None


def turn_of(thing):
    t = thing.get("turn") or 0
    return float(t[1] if isinstance(t, list) else t)


def rot_y(v, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    # right-handed, +Y up: a turn about Y (the same as Quat::axisAngle({0,1,0}, a))
    return [c * v[0] + s * v[2], v[1], -s * v[0] + c * v[2]]


def pose(thing):
    """Where a thing stands in the world and how far it's turned (through its parents)."""
    chain = []
    t = thing
    while t is not None:
        chain.append(t)
        t = things.get(t.get("parent"))
    pos, turn = [0.0, 0.0, 0.0], 0.0
    for t in reversed(chain):
        d = rot_y(t.get("at", [0, 0, 0]), turn)
        pos = [pos[i] + d[i] for i in range(3)]
        turn += turn_of(t)
    return pos, turn


def num(x):
    s = f"{x:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def vec(v):
    return f"vec({num(v[0])}, {num(v[1])}, {num(v[2])})"


def light_colour(l):
    c = l.get("color", [1, 1, 1])
    if "kelvin" in l and all(abs(x - 1) < 1e-3 for x in c):
        return f"kelvin: {num(l['kelvin'])}"
    if "kelvin" in l:
        return f"color: {vec(c)} * kelvin({num(l['kelvin'])})"
    return f"color: {vec(c)}"


lines = [
    "# The bar from Bar3D (joshuavelasco.com/bar), brought over from the original",
    "# repo's world files by tools/import_bar.py: don't edit by hand, run it again.",
    'title "The Bar"',
    'mode "game"',
    "",
]
air = place.get("air", {})
spawn = place["spawn"]
eye = spawn["at"]
fwd = rot_y([0, 0, -1], float(spawn.get("yaw", 0)))
lines += [
    "# where you come in",
    # a dim bar: the eyes adapt, a little darker than they would on their own
    f"camera at: {vec(eye)}, look_at: {vec([eye[i] + fwd[i] for i in range(3)])}, lens: 24 mm, exposure: -1.2",
    "# indoors: no sky, the air a little hazy",
    'sky "color", nits: 0',
]
if air.get("fogDensity"):
    # the original's fog was a tint added to the picture; here it's real haze
    # the lamps light, so it's thinner and nearly white
    lines.append(f"fog density: {num(air['fogDensity'] / 2)}, color: vec(0.9, 0.85, 0.8)")
lines += ['look "natural"', ""]

models, lights, walls = [], [], []


def rig_rule(lamp_id):
    """How each lamp lives (the original bar's light rig, assets/world/rigs/bar.looped):
    its shape over time, and whether the light switch dims it."""
    import fnmatch
    rules = [
        ("light-bulb-[1-4]", "breathe", "yes"), ("mirror-light", "breathe", "yes"),
        ("string-light-*", "strings", "yes"),
        ("light-arcade-neon-wave", "wave", "no"), ("light-neon-*", "neon", "no"), ("light-arcade-*", "neon", "no"),
        ("light-sign", "sign", "no"), ("light-fire", "fire", "no"), ("light-jukebox", "jukebox", "no"),
    ]
    for pattern, rule, dims in rules:
        if fnmatch.fnmatch(lamp_id, pattern):
            return rule, dims
    return "breathe", "no"  # lamps, sconces, spots: old filaments breathing
used = set()
for t in place["things"]:
    pos, turn = pose(t)
    m = part(t, "model")
    if m and m.get("file"):
        f = m["file"]
        if not (src / "assets" / "models" / f).exists():
            print(f"skipped {t['id']}: no model {f}")
            continue
        used.add(f)
        s = t.get("scale")
        args = [f'model "{f}"', f"at: {vec(pos)}"]
        if abs(turn) > 1e-6:
            args.append(f"turn: {num(turn)} deg")
        if s is not None and abs(float(s) - 1) > 1e-6:
            args.append(f"scale: {vec([float(s)] * 3)}")
        models.append(", ".join(args))
    l = part(t, "light")
    if l:
        lm = float(l.get("lumens", 100 * l.get("intensity", 1)))
        colour = light_colour(l)
        shape = l.get("shape", "point")
        centre = rot_y(l.get("center", [0, 0, 0]), turn)
        at = [pos[i] + centre[i] for i in range(3)]
        axis = rot_y(l.get("axis", [1, 0, 0]), turn)
        rule, dims = rig_rule(t["id"])
        def lamp(call):
            lights.append(f'lamp({call}, "{rule}", {dims})  # {t["id"]}')
        if shape == "panel":
            a2 = rot_y(l.get("axis2", [0, 1, 0]), turn)
            n = [axis[1] * a2[2] - axis[2] * a2[1], axis[2] * a2[0] - axis[0] * a2[2], axis[0] * a2[1] - axis[1] * a2[0]]
            lamp(f"panel_light(at: {vec(at)}, facing: {vec(n)}, size: vec({num(l.get('length', 0.5))}, {num(l.get('width', 0.2))}, 0), lumens: {num(lm)}, {colour})")
        elif shape == "tube":
            # a glowing tube: a row of small lamps along it, sharing its light
            length = float(l.get("length", 1))
            count = max(1, min(4, round(length / 0.8)))
            for k in range(count):
                f = (k + 0.5) / count - 0.5
                p = [at[i] + axis[i] * length * f for i in range(3)]
                lamp(f"light({vec(p)}, lumens: {num(lm / count)}, {colour}, radius: 0.03)")
        else:
            lamp(f"light({vec(at)}, lumens: {num(lm)}, {colour}, radius: {num(min(float(l.get('radius', 0.05)), 0.3))})")
    for b in part(t, "blocks") or []:
        x0, z0, x1, z1 = (float(v) for v in b[:4])
        walls.append(f"blocker size: vec({num(abs(x1 - x0))}, 2.6, {num(abs(z1 - z0))}), at: vec({num((x0 + x1) / 2)}, 1.3, {num((z0 + z1) / 2)})")
    for fl in part(t, "floors") or []:
        x0, z0, x1, z1, h = (float(v) for v in fl[:5])
        walls.append(f"blocker size: vec({num(abs(x1 - x0))}, {num(h)}, {num(abs(z1 - z0))}), at: vec({num((x0 + x1) / 2)}, {num(h / 2)}, {num((z0 + z1) / 2)})  # a raised floor: step up onto it")
    for sd in part(t, "solids") or []:
        half = sd.get("half", [0, 0, 0])
        if max(half) < 0.08 or t["kind"] in ("bottle", "glass", "pint_glass", "rocks_glass"):
            continue  # small things: walk past them
        c = rot_y(sd.get("at", [0, 0, 0]), turn)
        walls.append(f"blocker size: {vec([2 * h for h in half])}, at: {vec([pos[i] + c[i] for i in range(3)])}, turn: {num(turn)} deg")

# the same box twice (a raised floor listed as a floor and again as a solid) is one wall, not two
_seen, _once = set(), []
for w in walls:
    key = w.split("  #")[0].replace(", turn: 0 deg", "")
    if key not in _seen:
        _seen.add(key)
        _once.append(w)
walls = _once

switch = next((pose(t)[0] for t in place["things"] if t["kind"] == "light-switch"), [0, 1.2, 0])
fire = next((pose(t)[0] for t in place["things"] if t["kind"] == "light-fire"), None)
rig = f"""# ---- the bar's life: every lamp breathes, buzzes or waves on its own
# (the original's light rig), the light switch dims the bar
let lamps = []
let seeds = 0
to lamp(l, rule, dims)
  seeds += 1
  lamps.push({{light: l, base: l.lumens, rule: rule, dims: dims, seed: seeds * 7.31}})
  return l

to breathe(t, seed, amount: 6%, period: 8 s, speed: 0.5)
  return 1 + amount * noise(t * speed * 4 / period, seed, 0)
to buzz(t, seed, amount: 12%, period: 2 s, speed: 1)
  return 1 + amount * flicker(t, speed * 4 / period, seed)

to shine(l, t)
  if l.rule == "breathe"
    return breathe(t, l.seed)
  if l.rule == "strings"
    return breathe(t, l.seed, amount: 4%, period: 9 s, speed: 0.4)
  if l.rule == "neon"
    return buzz(t, l.seed, speed: 0.8)
  if l.rule == "wave"
    return 1 + 18% * wave(t, 5 s)
  if l.rule == "sign"
    return buzz(t, l.seed, amount: 60%, period: 1.2 s)
  if l.rule == "fire"
    return buzz(t, l.seed, amount: 40%, period: 0.5 s)
  if l.rule == "jukebox"
    return 1 + 20% * wave(t, 3 s)
  return 1
"""
life = f"""
let dim = 1
let dimTarget = 1
every 1 / 30 s
  dim = approach(dim, dimTarget, 2.5, 1 / 30)
  let t = time()
  for each l in lamps
    let k = shine(l, t)
    if l.dims
      k = k * dim
    l.light.lumens = l.base * k

# the light switch by the door: lights down over the bar, and up again
when "use"
  if near({vec(switch)}, within: 1.5 m)
    dimTarget = 25% if dimTarget == 1 else 1
"""
sounds = ["", "# the room's sound", "room size: 0.5"]
if fire:
    sounds.append(f'sound "fire", at: {vec(fire)}, volume: 0.6')
lines += [f"# {len(models)} things", *models, "", rig, f"# {len(lights)} lamps (lumens and kelvin as in the bar)", *lights, life, *sounds, "",
          f"# {len(walls)} walls, furniture and raised floors to bump into (invisible)", *walls, ""]
(out / "bar.loop").write_text("\n".join(lines))

for f in sorted(used):
    shutil.copy(src / "assets" / "models" / f, out / "models" / f)
for old in (out / "models").glob("*.glb"):
    if old.name not in used:
        old.unlink()
# real values for the models' materials (the original measured them: colours
# too dark or bright, floors made glossier than real), for the files we use
albedo = json.loads((src / "assets" / "models" / "albedo.json").read_text())
real = {}
for f in sorted(used):
    mats = {}
    for name, v in albedo.get(f, {}).items():
        e = {k: v[k] for k in ("fix", "rough") if k in v and v[k] not in (0, 1)}
        if e:
            mats[name] = e
    if mats:
        real[f] = mats
(out / "models" / "materials.json").write_text(json.dumps(real, indent=1) + "\n")
credits = (src / "assets" / "models" / "CREDITS.md").read_text()
(out / "models" / "CREDITS.md").write_text(credits.replace("Models are built by scripts in `tools/assets/`", "Models are built by scripts in the original repo's `tools/assets/` (Barcode162/3D)"))
manifest = out / "project.json"
if not manifest.exists():
    manifest.write_text(json.dumps({
        "order": 1, "name": "bar3d", "title": "Bar3D",
        "description": "The bar from joshuavelasco.com/bar, on LOOP3D.",
        "start": "bar", "scenes": ["bar"], "stills": "stills",
    }, indent=2) + "\n")
print(f"{len(models)} things, {len(lights)} lamps, {len(walls)} blockers, {len(used)} models -> {out}")
