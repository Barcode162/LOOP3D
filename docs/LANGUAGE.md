# Looped Script 2

Looped Script is Loop3D's own language. It reads like plain instructions, and it's a complete programming language. Files end in `.loop`.

```
title "The Cabin"
let oak = material("oak")
box size: vec(1.6, 0.06, 0.9), at: vec(0, 0.74, 0), material: oak
light vec(0, 1, 0), lumens: 14, kelvin: 1850
every 1 / 30 s
  fire.lumens = 1500 * (1 + 0.3 * flicker(time(), 7, 3))
```

## The basics

- **Lines and blocks.** One statement per line. A line ending a header (`if`, `to`, `every`…) owns the indented lines under it (two spaces). A `:` after the header is optional; with it, a short body can go on the same line: `if dark: say "night"`.
- **Comments** start with `#`.
- **Numbers carry units**, turned into metres, seconds and radians as they're read:
  - length: `m`, `cm`, `mm`, `km`
  - time: `s`, `ms`, `min`, `h`
  - angle: `deg`, `degrees`, `rad`
  - percent: `60%` (that's 0.6)
  - also `K` (colour temperature), `lumens`/`lm`, `lux`, `nits`, `kg`, `g`, `Hz`, `kmh`, `mph`
  - lenses: `lens: 50 mm`
- **Text** goes in `"…"`; `{expression}` inside is filled in: `"Hello {name}, {embers * 2} embers"`. Write `{{` for a plain `{`.
- **True and false:** `true`/`yes`, `false`/`no`. Nothing is `nothing` (or `nil`).
- **Comparisons:** `==`, `!=`, `<`, `<=`, `>`, `>=`, and in words `is` and `is not`; `and`, `or`, `not`; `x in list` checks whether it's there.
- **Maths:** `+ - * / %`; `^` raises to a power. Vectors add, subtract and scale: `vec(1, 2, 3) * 2`. Text joins with `+`.

## Names, functions, closures

```
let height = 2 m            # a new name
height = height + 10 cm     # change it (also += -= *= /=)

to greet(who, loud: false)  # a function; `loud` has a default
  if loud
    return "HELLO " + who
  return "hello " + who

say greet("Mira")
say greet(who: "Mira", loud: true)   # settings by name
say greet "Mira"                     # a call without brackets, as a command

let double = fn(x): x * 2            # a function as a value
to counter()
  let n = 0
  return fn(): n += 1                # remembers its own n (a closure)
```

Setting names can be several words; they're joined with `_`: `camera look at: vec(0, 1, 0)` is the same as `look_at:`.

## Choosing and repeating

```
if lamps > 3
  say "bright"
else if lamps > 0          # `otherwise` works too
  say "dim"
else
  say "dark"

let mood = "day" if light else "night"

for i from 1 to 10          # counts 1..10; `by 2`, or counts down by itself
for each tree in trees      # lists, tables (their keys), text, a number (0..n-1)
repeat 3 times
repeat forever
while cond
break / continue
```

## Lists and tables

```
let l = [3, 1, 2]
l.push(4)   l.pop()   l.sort()   l.reverse()   l.shuffle()   l.pick()
l.insert(0, x)   l.remove(x)   l.remove_at(i)   l.contains(x)   l.index_of(x)
l.map(fn(x): x * 2)   l.filter(fn(x): x > 1)   l.find(...)   l.each(...)   l.sum()
l.join(", ")   l.slice(1, 3)   l.copy()   l.clear()   l.length   l.first   l.last
l[0]   l[-1]                     # numbered from 0; -1 is the last

let mira = {name: "Mira", age: 9}
mira.age += 1       mira["town"] = "Emberhollow"
mira.keys()   mira.values()   mira.has("age")   mira.remove("age")   mira.size()

"text".upper()   .lower()   .split(" ")   .contains("x")   .replace("a", "b")
.starts_with(..)   .ends_with(..)   .trim()   .repeat(3)   .length
```

## Time: events, timers and waiting

Many things can run at once; each `every`, `after`, `when` and `task` is its own flow, and any of them can `wait`.

```
every 0.5 s                 # again and again
  lamp.lumens = 800 * (1 + 0.1 * wave(time(), 4))
after 3 s                   # once, later
  say "the door creaks"
when "door opened"          # whenever this event happens (`event` holds what came with it)
  say "creak"
emit "door opened"          # send an event
task                        # start something that runs alongside
  wait 2 s
  say "two seconds later"
```

A loop that runs too long without waiting is stopped, with its line, instead of freezing the game.

## Built-in words

- **Output:** `say`, `print`.
- **Maths:** `sqrt sin cos tan asin acos atan floor ceil round exp log sign abs min max clamp lerp smoothstep degrees radians`, `pi`, `tau`, `infinity`.
- **Vectors and colours:** `vec(x, y, z)`; `rgb(r, g, b)` (0..1 or 0..255, as seen); `hex("#ff8800")`; `kelvin(2700)`; `grey(0.5)`; `dot cross distance normalize length`; `v.x v.y v.z v.length v.normalized`.
- **Smooth motion:**
  - `ease(t, "smooth")`, with curves `linear in out in_out smoother sine back elastic bounce spring`
  - `approach(current, target, speed, dt)`: follow smoothly, never overshooting
- **Chance and variety:**
  - `random()`, `random(high)`, `random(low, high)`, `random_int(low, high)`, `chance(30%)`, `seed(n)`
  - `noise(x, y, z)`: smooth randomness
  - `wave(t, period)` and `flicker(t, speed, seed)`
- **Other:** `time()`, `text(v)`, `number(text)`, `type(v)`, `range(n)` / `range(from, to, by)`, `keys(table)`, `list(...)`.
- Every built-in word and its help: `loop words json` (the editor's suggestions come from the same list).

## Making a world

Every setting has a sensible default; give only what matters.

- **About the piece:**
  - `title "…"`
  - `mode "movie"` (plays its shots) or `mode "game"` (you walk around)
- **Materials:** `material("oak")`, or `material(like: "oak", roughness: 0.3)`, or `material(color: rgb(..), roughness, metal, glass, ior, dispersion, glow, clearcoat, sheen, subsurface, subsurface_color, translucent, opacity, leaf, pattern, pattern_color, pattern_scale, bump)`.
  - Patterns: `checker tiles bricks wood planks marble noise ground`.
  - Every shape takes `material:` as a name, a material, or a bare colour.
- **59 named materials** (`materials()` lists them):
  - wood: oak, pine, walnut, cherry, floorboards, old wood, bark
  - stone and building: marble, granite, stone, brick, concrete, plaster, tiles, porcelain, clay
  - metals: gold, silver, copper, brass, bronze, iron, steel, chrome, aluminium, rust
  - glass, water and ice: glass, crystal, frosted glass, green glass, water, ice
  - soft things: fabric, velvet, wool, leather, rubber, plastic, car paint, paper, wax, skin, hair
  - nature: leaves, needles, autumn leaves, grass, soil, sand, snow, moss
  - glowing: flame, ember, neon, screen, lamp shade
  - plain: white, black, grey
- **Shapes** (all take `at`, `turn`, `tilt`, `rotation`, `scale`, `material` (or `made_of`), `name`, and `collide: no` to walk through):
  - `box(size: vec(w, h, d), round)`, `sphere(radius)`, `cylinder(radius, height)`, `cone(radius, height)`, `capsule(radius, height)`, `ring(radius, thickness)`
  - `ground(size)` (its material also sets what footsteps sound like), `rock(size, seed)`
  - `tree(kind: "oak" | "pine" | "birch" | "bush", at, height, turn, variant, leaves)`
  - A shape's fields can be read and changed any time: `.position .turn .rotation .scale .visible .material`.
- **Land:**
  - `terrain(size, height, ridges, seed, erosion, rain, detail, feature, material)`: mountains and valleys carved by simulated rain (`detail` is scaled by the setting `world.terrainRes`)
  - `forest(kind, around, radius, spacing, clearing, clear_around, steepest, seed, leaves)` (trees per area follow `world.foliageDensity`)
  - `height_at(vec(x, 0, z))`
- **Light** (real units):
  - `light(at, lumens, kelvin | color, radius)`; add `aim:` and `angle:` (and `softness:`) for a spotlight; `aim_is_direction: yes` reads `aim` as a direction
  - `panel_light(at, facing, size, lumens, kelvin | color)`
  - `sun(elevation, toward (or azimuth), lux, kelvin, size: 0.53 deg)`: a wider sun casts softer shadows
  - `sky("physical", haze, stars, brightness, ground)` or `sky("color", color, nits)`
  - `fog(density, top, glow, color, from, to)`
  - Lights' fields: `.lumens .color .kelvin .position .aim`
- **Camera and look:**
  - `camera(at, look_at, lens: 35 mm, f_stop (or aperture), focus, shutter, iso, exposure, auto_exposure, sensor: "ff" | "s35" | "apsc" | "m43" | "imax", blades)`
  - `look("natural" | "blockbuster" | "noir" | "golden" | "bleach" | "seventies" | "neon" | "dream" | "documentary" | "horror" | "arctic" | "western", …)`: the preset first, then any of `contrast, saturation, warmth, tint, bloom, bloom_size, halation, grain, grain_size, vignette, bars, tone: "agx" | "aces" | "filmic" | "linear", lift, gamma, gain, shadow_tint, highlight_tint, black_level, fringes, distortion, sharpen, monochrome`
- **Shots** (they play in order):
  - `shot(name, length, from: {at, look_at, lens, f_stop, focus}, to: {...}, ease, shake, fade_in, fade_out, look)`
  - `orbit(name, length, around, radius, height, from, to, lens, f_stop, look_at, ease, shake, fade_in, fade_out)`
- **Physics:**
  - `solid(shape, mass, bounce, friction, velocity)`: it falls, rolls and knocks, and knocks make the material's sound by themselves (bounce and friction default to the settings `physics.bounce` and `physics.friction`)
  - `push(shape, vec(…))`, `gravity(vec(0, -9.81, 0))` (the setting `physics.gravity`, for this scene)
  - Simple shapes are things to bump into (unless `collide: no`).
- **People:**
  - `person(at, height, skin, shirt, trousers, facing)`: breathes, shifts weight, walks and runs with planted feet, swings their arms, and their footsteps match the floor
  - Fields: `.walk_to = vec(..)`, `.look_at = vec(..)` (or `nothing`), `.sitting = yes`, `.speed`, `.position`, `.facing`, `.walking`, `.visible`, `.lift` (metres above the ground; also `person(…, lift: 0.6)`)
- **Sound:**
  - `sound("fire" | "wind" | "rain" | "stream" | "birds" | "crickets" | "night" | "hum" | "applause" | "crowd" | "strings" | "chains" | "synth" | "drones" | "heartbeat", at, volume)` for endless sounds; without `at:` it's everywhere
  - `sound("footstep" | "impact" | "bell" | "click" | "thud" | "whoosh", variant, at, strength)` for one-offs (variants: wood, metal, glass, stone, grass, gravel, snow, water, carpet, plastic); endless sounds take `intensity`
  - Endless sounds play at the volume `audio.ambience`, one-offs and knocks at `audio.effects`, footsteps at `audio.footsteps`
  - Fields: `.volume .position .playing`
  - `room(size, echo)` sets how big the space sounds.
- **Story** (see [`STORY_GUIDE.md`](STORY_GUIDE.md)):
  - `line(speaker, text, seconds)` shows a line and gives back its seconds (by default long enough to read at `story.textSpeed`, at least `story.lineMin`); `clear_lines()` takes them away; `ask(question, [options])` sends `"choice"` with `event` = the option picked
  - `go_to("next scene")`; `remember(key, value)`, `recall(key, otherwise)` and `forget_story()` for what the story keeps between scenes
  - `player()`, `near(place, within)`, `move_player(at, look_at)`; pressing Use sends `"use"`
  - `use "common"` at the start of a line brings in `common.loop` from the same folder
- **Renders** (made by `loop render`):
  - `render(file, quality: "draft" | "good" | "cinema" | "ultra", width, height, samples, at)` for a still
  - `film(folder, quality, width, height, samples, fps)` for every frame of the shots
  - Their defaults, and bounces, noise removal, dispersion and motion blur, come from the `cinema.*` settings
- **Settings** (every one is in [`SETTINGS.md`](SETTINGS.md), by the same name as in the editor):
  - `setting "physics.iterations", 24`: changes an engine setting for this scene
  - `settings {story_textSpeed: 20, "audio.reverb": 1.5}`: several at once (`_` or `.` between the parts)
  - `get_setting("player.walk")`: a setting's value in effect
  - A scene's settings sit over the project's (`project.json`, `"settings": {…}`); the editor's live changes sit over both. Each new scene starts from the project's again.

## When something's wrong

Every problem names the file, the line, what's wrong in plain words and, when it can, "did you mean…?":

```
story.loop line 12: `heigth` isn't known; did you mean `height`?
story.loop line 30: there's no material called "oke"; did you mean "oak"?
```

`loop check file.loop` reads a script and reports its problems without running it.
