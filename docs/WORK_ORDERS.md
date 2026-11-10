# Work orders

How the roadmap (`docs/ROADMAP.md`) is cut into pieces a smaller agent can finish alone, and the pieces. The
lead (the strongest model, in the user's chat) writes and hands out orders; the four hands in `.claude/agents/`
carry them. An order's code is its track letter and a number (S3, W12); a library entry's is `L-` and its name.

## 1. The rules of an order

1. **One order, one outcome, one part.** It changes one part of `engine/modules.json` (or one library entry),
   and ends in a command that passes or fails. If it needs two parts, it is two orders.
2. **It names its files.** The agent may create or change only those, plus its own test. Needing another file
   means stop and report; the lead decides.
3. **It never decides design.** Names of words, formats and seams come from the order or from the docs it
   cites. If something is not decided, stop and ask the lead.
4. **Look before adding** ("one of everything"): search for an existing function or word first, and say in
   the report what was reused.
5. **It keeps everything else the same.** Quick checks pass. If the picture could change:
   `xvfb-run -a node tools/picture_check.js` passes, or the order says which pictures change and the new
   references are part of it. If speed could change: `loop budget` before and after, in the report.
6. **A new word** gets its help line (it then appears in `loop words` and the editor by itself), a line in
   `docs/LANGUAGE.md`, and a test in `tests/scene_api_test.cpp` or its own test file.
7. **A new setting** follows `docs/SETTINGS.md` and the editor's schema (`node tests/editor_source_test.js`
   fails if they disagree).
8. **Plain words** in comments, help and docs, as everywhere in this repo. No secrets, no unlicensed assets.
9. **The report** (the agent's last message): what was done, files changed, the proof command and its output's
   last lines, what was reused, anything odd seen, anything left undone. No "done" without the proof's output.
10. **It does not push.** The lead reviews, runs the checks once more, commits, logs and pushes.

Sizes: **S** under 150 changed lines, **M** under 400. Anything larger is cut again. Orders marked **lead**
need the strongest model (design or wide judgement); the rest are for the builder or the asset-smith.

## 2. The order card

```
CODE · title                                   hand: builder | asset-smith | lead     size: S | M
Needs:   orders that must be merged first
Read:    the few pages or files to read first (never "the codebase")
Touch:   the files it may create or change
Do:      numbered steps, each checkable
Proof:   the command(s) that must pass, and what their output must show
Not:     what is out of scope (the next order's work)
```

The quick checks, named `QUICK` below:
`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ./build/loop_tests && node tests/editor_source_test.js && node tests/recorder_test.js`

## 3. The flow

1. The lead picks the next rung, writes or copies its order cards, and starts one agent per card (cards that
   touch different files may run side by side; each in its own worktree).
2. Each agent reads its card, does it, runs the proof, reports.
3. The **checker** runs `QUICK` and, when the card says so, the picture tests, on the merged result.
4. When the picture should change, the lead renders it and the **judge** marks it against the rubric
   (`docs/CRAFT.md`, section 12). The judge's "one change" becomes the next card or is set aside with a reason.
5. The lead commits small, pushes, adds the `LOG.md` line, and shows the user the one rung with its one idea.

---

## 4. Milestone 0 · Many hands (track E)

Nothing the user sees changes: the same 12 pictures, the same speed. These are for the lead or a careful
builder, one at a time, because they move the most-touched code.

```
E1 · Shaders in their own files                                    hand: lead      size: M (several passes)
Read:   engine/gpu/shaders.h (its "// ----" section marks), CMakeLists.txt
Touch:  engine/gpu/shaders/*.wgsl.h (new), engine/gpu/shaders.h
Do:     1. Move each marked section (shadow, sky, clusters, lighting, bloom, rays, Ultra, Ultra's filter, and
           the shared helpers above them) to its own header of the same string constants.
        2. shaders.h only includes them. No text of any shader changes.
Proof:  QUICK; xvfb-run -a node tools/picture_check.js says 12 of 12 the same.
Not:    any change to what a shader does.
```
```
E2 · The renderer in parts                                         hand: lead      size: M (several passes)
Needs:  E1
Read:   engine/gpu/renderer.cpp, engine/render/frame_graph.h
Touch:  engine/gpu/renderer_*.cpp (new: setup, upload, shadows, clusters, light, post, ultra, screens later),
        engine/gpu/renderer_internal.h (new: the shared state), engine/gpu/renderer.cpp
Do:     One frame-graph step (or family) per file. Move code; change none. One file a commit.
Proof:  QUICK; the web player builds (tools/build_site.sh); picture tests 12 of 12 after each commit.
```
```
E3 · The world words in families                                   hand: lead      size: M (several passes)
Read:   engine/script/scene_api.cpp, engine/modules.json ("World words": to separate)
Touch:  engine/script/words_*.cpp (new: shapes, materials, lights, camera, nature, people, physics, sound,
        music, story, world, settings), engine/script/scene_api.cpp, engine/modules.json
Do:     Each family of vm.native(...) words to its own file with one register function; scene_api.cpp keeps
        the stage and calls them. loop words json must give the same 106 words with the same help.
Proof:  QUICK; `./build/loop words json` is byte-for-byte what it was (keep a copy before starting).
```
```
E4 · A word's test in one line                                     hand: builder   size: S
Needs:  E3
Touch:  tests/test.h, tests/words_test.cpp (new)
Do:     A helper runScene("…script…") that gives back the stage (things, lights, problems), so every later
        order tests its word in three lines. Move two existing tests onto it as examples.
Proof:  QUICK.
```
```
E5 · A picture of one library entry                                hand: builder   size: S
Touch:  tools/loop.cpp (a `loop library picture <name> [-o out.png] [--seed n]` command)
Do:     Runs the entry's `place:` line on a neutral floor under a soft sky and one sun, camera placed from the
        thing's size (three-quarter view, 35 mm), quality "good", 512 by 512.
Proof:  `./build/loop library picture lantern -o /tmp/l.png` writes a picture with the lantern filling
        about two thirds of it; QUICK.
```
```
E6 · The card's new lines                                          hand: builder   size: S
Read:   engine/project/library.cpp, docs/LIBRARY_PLAN.md section 2
Touch:  engine/project/library.*, tests/library_test.cpp, the 6 entries in library/
Do:     Optional card lines `tags:`, `size:` (hand | chair | sofa | room | land), `sockets:`, `seeds:`;
        `loop library check` prints each entry's triangle count and refuses one over its size's budget.
Proof:  QUICK; `./build/loop library check` lists 6 entries with counts.
```
```
E7 · The rubric file and the judge's sheet                         hand: lead      size: S
Touch:  docs/rubrics/*.md (new: base, hill, loft, valley, person, move, shot, screen), perf/judge.md (new)
Do:     Each rubric as ten numbered questions from docs/CRAFT.md section 12, with the reference described in
        words. perf/judge.md keeps every marking: date, picture, ten marks, total, the one change.
```
```
E8 · Check on GitHub runs the library pictures                     hand: builder   size: S
Needs:  E5
Touch:  .github/workflows/ (the Check workflow), tools/library_sheet.py (new)
Do:     After the tests, render every entry's picture into one contact sheet and keep it as an artifact.
Proof:  the workflow file passes its own syntax check; the sheet builds locally.
```
Also in milestone 0, written when reached: the pack's first half (Phase B.2), `loop budget` counting a cooked
library, a worktree-per-agent note in `.claude/agents/`, and the open phone confirmations in `PLAN.md`.

---

## 5. Track S · Shape words (first rungs written out)

`engine/geometry/mesh.h` already has `shapes::lathe` and `shapes::tube`; they are not yet words.

```
S1 · The word `lathe`                                              hand: builder   size: S
Needs:  E3, E4
Read:   engine/geometry/mesh.h (shapes::lathe), docs/LANGUAGE.md "Making a world"
Touch:  engine/script/words_shapes.cpp, docs/LANGUAGE.md, tests/words_test.cpp
Do:     1. `lathe(profile: [vec(r, y), …], segments: 32, at, turn, material, name, collide)`: a list of
           (radius, height) points spun round Y. Takes everything a shape takes.
        2. Mesh cached by its numbers like `box`. A point at radius 0 closes the end.
        3. Problems named plainly: fewer than two points, a negative radius.
Proof:  QUICK; a test places a vase from 6 points and finds one thing whose box is right to 1 mm.
Not:    smoothing the profile (S2).
```
```
S2 · Smooth profiles                                               hand: builder   size: S
Needs:  S1
Touch:  engine/geometry/curve.h/.cpp (new), engine/script/words_shapes.cpp, tests/geometry_test.cpp
Do:     `curve([points], smooth: yes, steps: 8)` gives back a list of points along a Catmull-Rom curve through
        them; a point given as `corner(vec)` stays sharp. lathe, extrude and sweep accept its result.
Proof:  QUICK; a test checks the curve passes through every given point and has the asked number of steps.
```
```
S3 · The word `extrude`                                            hand: builder   size: M
Needs:  S2
Touch:  engine/geometry/extrude.h/.cpp (new), engine/script/words_shapes.cpp, docs/LANGUAGE.md, tests
Do:     `extrude(outline: [vec(x, z), …], height, bevel: 0, holes: [[…]])`: the outline filled (ear clipping,
        holes bridged), pushed up, top and bottom edges bevelled by `bevel` in 3 steps. Normals smooth on the
        bevel, flat on faces. UVs in metres.
Proof:  QUICK; tests: a square gives a box's volume to 0.1%; an L shape; a frame (a square with a hole); every
        triangle faces outward (signed volume positive).
```
```
S4 · The word `sweep`                                              hand: builder   size: M
Needs:  S2
Touch:  engine/geometry/sweep.h/.cpp (new), words_shapes.cpp, docs/LANGUAGE.md, tests
Do:     `sweep(path: [vec, …], section: [vec(x, y), …] or radius, closed: no, caps: yes, twist: 0)`: the section
        carried along the path without twisting (parallel transport frames), mitred at corners.
Proof:  QUICK; tests: a straight sweep of a square equals a box; a closed sweep of a circle equals `ring`
        within 1% of volume; no flipped triangles on an S-shaped path.
```
```
S5 · Rounded edges on extrude and box, properly                    hand: builder   size: S
S6 · `mirror`, `array`, `along` (copies of a thing or a group)      hand: builder   size: M
S7 · `group` with `part` names; a group moves, turns, hides as one  hand: builder   size: M
S8 · `socket "seat", at, facing` on a group; `sockets(thing)` lists hand: builder   size: S
S9 · `carve(from, cut)` and `join` (boxes, cylinders, extrusions)   hand: lead      size: M
S10 · `soften(shape, puff: 0.03, seams: […])`: cushions             hand: lead      size: M
S11 · `drape(cloth size, over: things)`: dropped once, kept         hand: builder   size: M
S12 · `bend`, `twist`, `taper` on any mesh                          hand: builder   size: S
S13 · `displace(shape, by: noise, amount)` with rebuilt normals     hand: builder   size: S
S14 · `scatter(on: thing or socket, things: […], count, seed, gap)` hand: builder   size: M
S15 · `strands(on, count, length, curl, clump)`: fringe, fur, pile  hand: lead      size: M
S16 · `leaf_card` and branching rules opened to scripts             hand: lead      size: M
S17 · Automatic unwrap by faces' directions, grain along length     hand: builder   size: M
S18 · `variant(seed)` and ranges: `between(0.4, 0.5)` in a recipe   hand: builder   size: S
S19 · Stairs: `stair(rise, run, width, kind: straight|L|U|spiral)`  hand: builder   size: M
S20 · Rails and balusters along any path (uses sweep, along)        hand: builder   size: S
S21 · Window walls: `glazing(width, height, bars_x, bars_y, frame)` hand: builder   size: S
S22 · Walls with openings: `wall(length, height, openings: […])`    hand: builder   size: M
S23 · Roofs, beams, trusses                                         hand: builder   size: M
S24 · Mouldings: skirting, cornice, architrave as swept sections    hand: builder   size: S
```
Each of S5 to S24 is written out as a full card by the lead when it is next, in the same form as S1 to S4.

## 6. Track M · Surface recipes

```
M1 · The design of a surface recipe (words, layers, what the cook bakes)   hand: lead      size: doc + S
M2 · Curvature and hollows per vertex at cook time                 hand: builder   size: M
M3 · The layer stack evaluated in the film renderer                hand: lead      size: M
M4 · The cook bakes a recipe to colour, bump, roughness pictures   hand: builder   size: M
M5 · The live renderer reads baked pictures for recipe surfaces    hand: lead      size: M
M6 · Triplanar projection (film and live)                          hand: builder   size: M
M7 · The detail layer under two metres                             hand: builder   size: S
M8 · A test-ball scene and `loop library picture` for surfaces     hand: builder   size: S
M9 … M58 · Fifty surfaces, one a card (asset-smith, judged):
     wood: oak, walnut, pine, ash, ebony, plywood, weathered board, painted wood, bamboo, cork
     stone: granite, basalt with varnish, limestone, slate, sandstone, marble white, marble dark, travertine
     ground: decomposed granite, desert pavement, dry wash sand, forest floor, wet mud, gravel, turf, snow
     built: red brick, painted brick, cast concrete, polished concrete, plaster, terrazzo, ceramic tile, asphalt
     metal: brushed steel, blackened steel, brass aged, copper green, chrome, cast iron, galvanised, gold leaf
     soft: linen, boucle, velvet, wool knit, leather worn, suede, denim, canvas, shag pile, felt
M59 · Decals (film, then live)                                     hand: lead      size: M
M60 · Night glass: a pane that mirrors the room                    hand: lead      size: M
M61 · A short fur shell for rugs, throws, moss                     hand: lead      size: M
M62 · ASTC pictures made at pack time by a build tool              hand: builder   size: M
```

## 7. Track W · Land (milestone 1 first)

```
W1 · Fine carving guided by flow (ridges and gullies to 1 m)        hand: lead      size: M
W2 · Rock hardness layers in `world(...)`: caps, cliffs, benches    hand: lead      size: M
W3 · Preset "desert basin": numbers for the generator (plates quiet, rain 0.1, fans, a flat floor) hand: builder size: S
W4 · Boulders and scree: where loose rock rests (a slope walk)      hand: builder   size: M
W5 · Ground surface in three blended layers by slope and flow      hand: lead      size: M
W6 · Elbow-room scatter: each plant kind with its own radius       hand: builder   size: M
W7 · Ground cover near the player (pebbles, tufts, flowers)        hand: builder   size: M
W8 · The far view: coarse rings to the horizon                     hand: lead      size: M
W9 · Haze by distance and height (film and live, one formula)      hand: builder   size: S
W10 · Dirt roads and house plots on the flat (rules)               hand: builder   size: M
W11 · `loop world <preset> --check`: nature's rules for each preset hand: builder   size: S
W12 … W40 · alpine river, tower karst, plateau falls, cloud ridge, coast; terraces, paths, ruins; caves
```

## 8. Track A · Water and air

```
A1 · Haze layers behind ridges (with W9)                           hand: builder   size: S
A2 · Still water: depth colour and the bed seen through (film)     hand: lead      size: M
A3 · Still water live: refraction from the screen, sky reflection  hand: lead      size: M
A4 · River surfaces along the carved network                       hand: lead      size: M
A5 · Ripples that follow the current                               hand: builder   size: M
A6 · White water by steepness; wet banks                           hand: builder   size: S
A7 · Falls: sheets, threads, mist                                  hand: lead      size: M
A8 · A cloud layer lit from below, drawn over several frames       hand: lead      size: M
A9 · Valley mist with a flat top                                   hand: builder   size: S
A10 · Time of day as one dial                                      hand: builder   size: S
A11 · `skyline(...)`: a night city in layers behind glass          hand: builder   size: M
A12 … A30 · caustics, sea waves, shore foam, wading, rain, storm, snow, moon and sharp stars
```

## 9. Track U · Mechanics and screens (`docs/MECHANICS_UI.md`)

```
U1 · Letters from a font file as distance fields (the cook)        hand: lead      size: M
U2 · The "screens" step: panels, rings, letters in one draw        hand: lead      size: M
U3 · The layout solver, upright phone, with fake items             hand: lead      size: M
U4 · Picture tests for 1, 3, 7 items a zone                        hand: builder   size: S
U5 · `meter` and `counter`                                         hand: builder   size: M
U6 · `goal`                                                        hand: builder   size: S
U7 · `action` and the input list (thumb, key, pad)                 hand: builder   size: M
U8 · `prompt` tied to a place                                      hand: builder   size: S
U9 · `timer`, `message`                                            hand: builder   size: S
U10 · `bag` and its sheet                                          hand: builder   size: M
U11 · `map` from the land                                          hand: builder   size: M
U12 · Pause, goals, controls, credits (from library cards)         hand: builder   size: M
U13 · Themes as library entries; the first three                   hand: builder   size: M
U14 · Feel by role (count, flash, breathe)                         hand: builder   size: S
U15 · Wide phone and computer layouts                              hand: builder   size: M
U16 · Reading for everyone                                         hand: builder   size: S
U17 · The Lantern Isle moved onto it                               hand: builder   size: S
```

## 10. Tracks P, V, K, C, X (their first cards; the rest are cut when reached)

```
P1 · The standard skeleton: 65 bones, names, limits (a table)      hand: lead      size: M
P2 · The old 17-bone person mapped onto it; same walk, same picture hand: builder  size: M
P3 · Skin weights and smooth bending (film and live)               hand: lead      size: M
P4 · A body from numbers: limbs and torso as blended rounded forms hand: lead      size: M
P5 · Hands                                                         hand: lead      size: M
P6 · A head and face sliders                                       hand: lead      size: M

V1 · Clips as text: the format, a reader, a writer                 hand: builder   size: M
V2 · A BVH reader that fits a recording to the standard skeleton   hand: builder   size: M
V3 · Foot contacts found and locked; loops closed                  hand: builder   size: M
V4 · Layers and masks                                              hand: builder   size: M
V5 · The matcher (motion matching) for walking, turning, stopping  hand: lead      size: M
V6 … · one move a card for the asset-smith (`library/moves/`), each from a recording whose licence allows it

K1 · `kind` with properties and events; prefabs                    hand: lead      size: M
K2 · Saving and loading the world by the one clock                 hand: lead      size: M
K3 · Paths round obstacles (a walkable mesh from the cook)         hand: lead      size: M
K4 · The sheet read from the cover page; `loop check` on clashes   hand: builder   size: M
K5 · Kit "story walk" from the template that exists                hand: builder   size: M
K6 · A robot player that finishes a kit's sample                   hand: builder   size: M

C1 · Rigs as words: `rig("dolly" | "crane" | "handheld" | "follow" | …)` with named moves   hand: lead size: M
C2 · The follow camera: soft zone, spring, wall arm, look-ahead    hand: builder   size: M
C3 · Reframing a shot for upright and wide                         hand: builder   size: M
C4 · Pull focus and focus on a named thing                         hand: builder   size: S
C5 · `loop check` reads a scene's shots for the line and 30 degrees hand: builder  size: M
C6 · Light rigs as library entries                                 hand: asset-smith

X1 · Sound from files (a from-scratch reader for one free format)  hand: builder   size: M
X2 · Mixer groups and ducking                                      hand: builder   size: M
X3 · Surface-pair sounds for steps and knocks                      hand: builder   size: S
```

## 11. Library orders

One entry per order, for the asset-smith, in the order of `docs/LIBRARY_PLAN.md`. Its card is always:

```
L-<name> · <what it is>                                            hand: asset-smith
Needs:   the shape words and surfaces its recipe uses (the plan lists them)
Read:    docs/CRAFT.md sections 3 and 4; docs/LIBRARY_PLAN.md section 2; two existing entries of its kind
Touch:   library/<kinds>/<name>.loop only
Do:      the recipe with its whole card; true size; sockets; a seed range where every seed looks right
Proof:   ./build/loop library check; ./build/loop library picture <name> for three seeds; the judge's mark
         on the base rubric's questions 2, 3, 4 and 6 is 7 or more each
```

## 12. What to hand a small agent, and what not to

Good for a small agent: one word with a test; one library entry; one preset's numbers; one screen role; a
reader for a file format; moving code without changing it; docs that follow code; running checks.

Keep for the lead: anything that sets a format or a seam; anything touching two parts; shaders that change the
picture everywhere; the solver, the matcher, the carving; judging whether a picture is good; talking to the
user; deciding what "one of everything" means when two ways appear.

Signs an order was too big: the agent touched files off its list, invented a second way to do something that
exists, skipped the proof, or reported "mostly". Cut it in two and run it again.
