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
- **Materials:** `material("oak")`, or `material(like: "oak", roughness: 0.3)`, or `material(color: rgb(..), roughness, metal, glass, ior, dispersion, glow, clearcoat, sheen, subsurface, subsurface_color, translucent, opacity, leaf, pattern, pattern_color, pattern_scale, bump, and for `windows`: floor, bay, window, lit, seed)`.
  - Patterns: `checker tiles bricks wood planks marble noise ground grid windows` (`grid`: even squares with thin lines in `pattern_color`, one square per `1 / pattern_scale` metres; its lines stay sharp at any distance).
  - `windows`, in film: `material(pattern: "windows", floor: 3.2, bay: 2.4, window: vec(1.5, 1.7), lit: 0.35, glow: 40, seed: 1)` cuts a tower's face into cells one `floor` high (m) and one `bay` wide (m), a `window` (width, height in m) in each middle; none on roofs. Each window is lit or dark by its cell and the `seed`; `lit` is the share lit. A lit one glows `glow` nits times 0.3 to 1 (most warm, 2,700 to 4,000 K; one in eight cool, 6,500 K) and so gives light; a dark one is near black and smooth; between windows is the material's own colour. The live view draws it the same way (same cells and hash), blending to the average when a window or a cell is smaller than a pixel.
  - Every shape takes `material:` as a name, a material, or a bare colour.
  - `surface("oak", tile: 2 m, project: "box", space: "thing", along: "long", seed: 1, base: {color: rgb(..), roughness, metal, pattern: "grain", size: 6 mm, stretch, bump}, vary: {large, amount, tint, roughness, small, small_amount}, coat: {..}, coat2: {..}, wear: {..}, dirt: {..}, detail: {pattern, size, bump, tile, fade}, sheen, clearcoat, subsurface, subsurface_color, translucent, ior)` makes a material from a stack of layers in real sizes (`docs/SURFACES.md`); `base` is required, sizes are in metres (`6 mm`). It gives back a material: the film and the phone draw its whole stack (wear on rounded edges, dirt in creases and grooves, coats, slow change across metres). An unknown layer or setting fails by name with the nearest right one; a non-metal base colour outside 0.03 to 0.9, a `metal` between 0 and 1, a pattern with no `size` and two coats with a `facing` are refused.
  - `material("oak")` finds a surface first: one made in this scene, then `library/surfaces/oak.loop` (a name of several words, "old wood", is `old_wood.loop` with its function `old_wood()`), then the built-in list. Given its own `color`, `roughness` or `metal` (`material("plaster", color: rgb(..), roughness: 0.9)`), a surface keeps its layers: the colour tints its own colours, the roughness shifts its shine by as much, and its wear and dirt stay as they are; an old `pattern:` by name gives the flat built-in material, as before surfaces.
  - `material("oak")` and `material: "oak"` look for a surface made in this scene, then `library/surfaces/oak.loop` (its function `oak()` called once, the answer kept for the scene), then the built-in list; `materials()` lists all three.
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
  - `box(size: vec(w, h, d), round, steps)` (`round` rounds the edges; `steps` is how many flat pieces make each rounded edge: 1 under 5 mm, 2 under 20 mm, 4 above, unless you set it), `sphere(radius)`, `cylinder(radius, height, sides)` (`sides` is 16 for tubes under 3 cm across, 64 otherwise unless you set it), `cone(radius, height)`, `capsule(radius, height)`, `ring(radius, thickness)`
  - `lathe(profile: [vec(radius, height), …], segments: 32)`: an outline spun round the up axis, like a vase, bowl or lamp base; a point at radius 0 closes that end. It needs two points or more and no negative radius
  - `group at: vec(…), turn: 90 deg` with indented lines under it: everything those lines place (things, lamps, strips, groups inside) is measured from the group's place and turn, and a recipe called there lands in the group, which is how a library piece is turned. With a name: `let stair = group(at: …, turn: …)` and the lines under it; then `stair.position`, `stair.turn` and `stair.visible` move, turn and hide them all (its lamps go dark with it).
  - `ground(size)` (its material also sets what footsteps sound like; `collide: no` is seen but never stood on, like water), `rock(size, seed)`
  - `tree(kind: "oak" | "pine" | "birch" | "bush", at, height, turn, variant, leaves)`
  - A shape's fields can be read and changed any time: `.position .turn .rotation .scale .visible .material`.
- **Models** made in Blender or any tool that saves glTF binary (`.glb`):
  - `model "stool.glb", at: vec(…), turn: 30 deg` finds the file next to the scene or in its `models/` folder; read once, however many times it's placed
  - its colours, pictures (JPEG or PNG), metal, roughness and glow come with it; clear see-through parts become real glass
  - also takes `scale`, `name`, `material` (over every part), `glow:` (nits per unit of the model's glow, 8 by default) and `collide: yes` to bump into it
  - its pictures show in both the live player and `loop render` (the live player keeps each picture at `render.textureSize`, 256 pixels square by default)
- **Invisible walls:** `blocker size: vec(w, h, d), at: vec(…), turn: …` is a box you bump into that is never drawn (the walls of a room made as one model, furniture's footprint).
- **Land:**
  - `terrain(size, height, ridges, seed, erosion, rain, detail, feature, island, material)`: mountains and valleys carved by simulated rain (`detail` is scaled by the setting `world.terrainRes`); `island: yes` sinks the edges into a sea at height 0, so the middle is an island (put `ground …, material: "water", collide: no` at 0 for the sea)
  - `forest(kind, around, radius, spacing, clearing, clear_around, steepest, seed, leaves)` (trees per area follow `world.foliageDensity`)
  - `height_at(vec(x, 0, z))`
- **Nature, ruins and creatures** (the model library; every one is one handle with `.position` and `.visible`):
  - `grass(around, radius, density: 6 per m², height, clearing, clear_around, dry_share, material, seed)`: a meadow of curved blades on the land
  - `fern(at, size, turn, seed)`: arching fronds of leaflets
  - `flower("daisy" | "violet" | "poppy" | "bluebell", at, size, turn)` and `flowers(around, radius, count, kinds: [...], size, seed)`: wild flowers, in drifts
  - `column(at, height, radius, flutes, broken: yes, seed, material)`: a fluted stone column (`tilt: 90 deg` lays a drum on its side)
  - `log(at, length, radius, turn, tilt, seed)`: a fallen log with cut ends
  - `butterfly(around, radius, color: "orange" | "blue" | "white", size, speed, height)`: flutters from flower to flower, resting now and then; the engine moves it
  - `fireflies(around, radius, count, height, size)`: glowing motes that drift and twinkle
  - `tree(…)` gives back the tree; `tree` and `forest` take `leaf_size:` (smaller leaves come in greater numbers)
  - Materials for them: `fern`, `fresh leaves`, `blades`, `dry blades`, `daisy`, `violet`, `poppy`, `bluebell`, `pollen`, `stem`, `wing blue`, `wing orange`, `wing white`, `firefly`, `ruin stone`, `mossy stone`, `forest floor`
- **Light** (real units):
  - `light(at, lumens, kelvin | color, radius, shadows: yes)`; `shadows: no` makes a lamp that casts none (no shadow map, no ray); add `aim:` and `angle:` (and `softness:`) for a spotlight; `aim_is_direction: yes` reads `aim` as a direction
  - `panel_light(at, facing, size, lumens, kelvin | color, shadows: yes)`; `shadows: no` makes a panel that casts none
  - `strip(from, to, lumens: 300, kelvin: 2700, facing: vec(0, -1, 0), width: 0.012, piece: 0.25, shadows: no, name)`: a thin strip of light; `lumens` is for each metre. One glowing rectangle (on a dark channel) and a row of panel lamps (each at most `piece` long) that stand for it on the phone; film lights from the rectangle alone. Gives back the glowing rectangle
  - `sun(elevation, toward (or azimuth), lux, kelvin, size: 0.53 deg)`: a wider sun casts softer shadows
  - `sky("physical", haze, stars, brightness, ground)` or `sky("color", color, nits)`
  - `fog(density, top, glow, color, from, to)`: mist you can stand in
  - `haze(visibility: 60 km, height: 1.5 km)`: distant air, off unless a scene says it. A level sight line `visibility` long leaves 2% of a thing's green; red is lost a quarter slower and blue half as fast again, so far dark things go blue first, then pale, towards the sky's colour at the horizon (a little brighter towards the sun). The air thins with height (`exp(-y / height)`), so from a hill the same distance loses less. Film and phone use the same formula (`hazeAt`)
  - Lights' fields: `.lumens .color .kelvin .position .aim`
- **Camera and look:**
  - `camera(at, look_at, lens: 35 mm, f_stop (or aperture), focus, shutter, iso, exposure, auto_exposure, sensor: "ff" | "s35" | "apsc" | "m43" | "imax", blades)`
  - `look("natural" | "blockbuster" | "noir" | "golden" | "bleach" | "seventies" | "neon" | "dream" | "documentary" | "horror" | "arctic" | "western", …)`: the preset first, then any of `contrast, saturation, warmth, tint, bloom, bloom_size, halation, grain, grain_size, vignette, bars, tone: "agx" | "aces" | "filmic" | "linear", lift, gamma, gain, shadow_tint, highlight_tint, black_level, fringes, distortion, sharpen, monochrome`
- **Shots** (they play in order):
  - `shot(name, length, from: {at, look_at, lens, f_stop, focus}, to: {...}, ease, shake, fade_in, fade_out, look)`
  - `orbit(name, length, around, radius, height, from, to, lens, f_stop, look_at, ease, shake, fade_in, fade_out)`
- **Keys** (movement made by hand; `docs/STUDIO.md`): a key pins a value to a moment, and the engine fills in every moment between.
  - `clip crate` with `key` lines indented under it moves a thing, a lamp or a group. Name the thing first: `let crate = box(size: vec(1, 1, 1))`.
  - `key 2 s, at: vec(2, 0.5, 0), turn: 90 deg`: at 2 seconds, be there, turned so. A thing's keys: `at`, `turn`, `tilt`, `roll`, `scale`, `visible`; a lamp's: `at`, `lumens`, `color`, `kelvin`; a group's: `at`, `turn`, `visible`. A value with no key keeps what its own line gave it.
  - `ease:` is how the values arrive at that key from the one before: `"smooth"` (the usual way: through the keys with no overshoot, starting gently and landing softly), `"line"` (straight), `"hold"` (stay, then jump), or an ease by name (`in out in_out smoother sine back elastic bounce spring`).
  - `in:` and `out:` give one value its own slopes at a key (in units a second), and then the curve may overshoot: `key 1 s, at: vec(0, 1.25, 0), in: vec(0, 0.5, 0), out: vec(0, -0.5, 0)`.
  - On the `clip` line: `loop: yes` (round again; the last key meets the first with no jolt), `loop: "back and forth"`, `length: 4 s` (longer than its last key), `rate: 30` (frames a second, where the editor puts keys), `play: no` (wait to be started), `speed: 1`.
  - The clip is a value: `let swing = clip(door, play: no)`, then `swing.play()`, `swing.reverse()`, `swing.stop()`, and its fields `.playing .time .speed .length .loop`. A door that opens when used: `when "use"` then `swing.play()`.
  - Keys written inside a `group` are measured from the group, and move and turn with it. A `solid` thing is moved by physics and takes no keys. A thing moved by keys is something to bump into where it comes to rest.
  - `clip "wave", for: "human"` is a clip by name for a skeleton: each `key` turns bones by `vec(bend, side, twist)`.
- **Skeletons** (the bones a body is hung on; `docs/STUDIO.md`):
  - `skeleton "human"` with `bone` lines indented under it writes a skeleton down by name. Every bone called `left_…` gets its `right_…` twin by itself (`mirror: no` stops that).
  - `bone "left_forearm", parent: "left_upper_arm", from: vec(…), to: vec(…), bends: vec(0, 0, -1), bend: [-5 deg, 150 deg], twist: [-160 deg, 10 deg]`: where the bone starts and ends when the body stands at rest (metres; facing -z, its left at -x), the way a positive bend carries its tip, and how far it turns from rest, each as `[least, most]`: `bend` (like a nod), `side` (like a wave), `twist` (like a doorknob). A turn not named does not happen: an elbow has no `side`. `free: yes` is a bone with no limits (the root, the hips). A bone comes after the one it hangs from.
  - `use "human"` brings in the library's standard human (67 bones, 1.75 m); `bones("human", at: vec(0, 0, 0), turn: 0, height: 1.75, material: "porcelain")` shows a skeleton standing in its rest pose, each bone a slim shape from its start to its tip.
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
- **Music** (the engine's own instruments, made from scratch; it plays in time with the world, at the volume `audio.music`):
  - `tempo 100, swing: 0.1`: beats a minute (4 steps a beat, 16 a bar) and a shuffle (0 straight .. 0.5)
  - `track "kick", "x...x...x...x..."`: a line of music that loops over its own length; drums take one character a step (`x` a hit, `X` a loud one, `.` nothing)
  - `track "bass", "C2 . . Eb2 - - G1 .", volume: 0.8`: notes take one word a step (a note like `C4`, `Eb3`, `F#2`, `G2!` loud; `.` a rest; `-` holds the note before)
  - Instruments: drums `kick snare hat open_hat clap tom`; with notes `bass lead pad pluck keys bell`
  - The editor's Sound space writes these lines with a drum grid and a melody grid
- **Story** (see [`STORY_GUIDE.md`](STORY_GUIDE.md)):
  - `line(speaker, text, seconds)` shows a line and gives back its seconds (by default long enough to read at `story.textSpeed`, at least `story.lineMin`); `clear_lines()` takes them away; `ask(question, [options])` sends `"choice"` with `event` = the option picked
  - `go_to("next scene")`; `remember(key, value)`, `recall(key, otherwise)` and `forget_story()` for what the story keeps between scenes
  - `player()`, `near(place, within)`, `move_player(at, look_at)`; pressing Use sends `"use"`
  - The player stands on the land and on the top of any fixed thing underfoot (a dock, a bridge, a stair), stepping up as high as `physics.charStep`
  - `use "common"` at the start of a line brings in `common.loop` from the same folder; in a project's `places/`, a script whose name ends in `_common` holds what the scenes share and is never listed as a scene
  - **The library** (`library/`, shared by every project): `use "lantern"` brings in a library entry by name when the scene's folder has no script of that name. Each entry starts with its card (what it is, how to place it, its credit and licence, where it came from); `loop library list` shows them, and the editor's Build space places them with a tap. Entries so far: `lantern`, `sailing_boat` (models), `room_shell` (rooms), `plinth`, `lintel` (pieces)
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
