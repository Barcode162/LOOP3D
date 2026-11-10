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

The design is `docs/SURFACES.md` (M1, done 2026-11-10): the word `surface`, the fixed stack of layers, the
patterns, the two marks a vertex, the nine steps of a point, what is baked, what the phone reads. Every card
below builds one part of that page and decides nothing. M3 was too big for one order and is cut in three.

```
M1 · The design of a surface recipe                                hand: lead      done: docs/SURFACES.md
```
```
M2 · The marks: edge and hollow per vertex at cook time             hand: builder   size: M
Needs:  M1
Read:   docs/SURFACES.md section 6; engine/geometry/cook.h, cook.cpp; engine/geometry/bvh.h
Touch:  engine/geometry/marks.h, marks.cpp (new), engine/geometry/cook.h, cook.cpp, tests/marks_test.cpp (new)
Do:     1. `struct Marks { std::vector<uint8_t> edge, hollow; }` and `Marks measureMarks(const Mesh&)`,
           following section 6 step by step: weld by position; the sharpest bend through each vertex with
           its sign; `edge` and `hollow` from it at 1 cm; `hollow` raised by 16 rays of 10 cm against the
           same mesh (a Bvh over its triangles, rays start 1 mm off the surface); each lowered by
           `clamp(2 cm / a)` for the largest triangle at the vertex. A vertex welded from several keeps
           one answer for all of them.
        2. `Cook::marks(const Mesh&)`: made the first time it is asked, kept by the fingerprint, safe
           from any thread, exactly as `clusters`. Never made unless asked.
        3. 100,000 triangles in under 300 ms on one core; say the time in the report.
Proof:  QUICK; tests, marks as bytes 0 to 255:
        a. `shapes::roundedBox({1, 1, 1}, 0.005, 4)`: every vertex strictly inside a rounding has edge
           of 200 or more; every vertex on the border of a flat face 25 or less; no hollow above 25.
        b. `shapes::box({1, 1, 1})`: no edge above 25 (a knife edge does not wear).
        c. `shapes::sphere(0.5)`: no edge above 25. `shapes::sphere(0.005)`: every edge 200 or more.
        d. A cup from `shapes::lathe` (8 cm across, 10 cm deep, walls 3 mm): the inside of its floor has
           hollow of 128 or more; its outside wall 25 or less.
        e. Two 1 m planes meeting in an inside corner filleted at 5 mm in 4 steps: the fillet's inner
           vertices have hollow of 200 or more; the planes' far corners 0.
        f. The same mesh twice gives the same bytes; the second ask does no work (`Cook::kept`).
Not:    reading the marks anywhere (M3c in film, M5 on the phone); any change to the vertex format.
```
```
M3a · The word `surface` and where a material's name is looked up   hand: builder   size: M
Needs:  M1, E4
Read:   docs/SURFACES.md sections 2, 3, 4 and 12; engine/script/words_materials.cpp; `applyMaterialSetting`
        and `materialFrom` in engine/script/scene_api.cpp; engine/project/library.h
Touch:  engine/render/surface.h, surface.cpp (new), engine/render/scene.h (two fields), engine/render/pattern.cpp
        (`shadeSurface`: one branch), engine/script/words_materials.cpp, engine/script/scene_api.cpp
        (`materialFrom` only), engine/modules.json, docs/LANGUAGE.md, tests/words_test.cpp
Do:     1. `surface.h` with the structs of section 12 exactly, and `SurfaceRecipe::fingerprint()` over every
           number in it. `Scene::surfaces`, `Material::surface = -1`.
        2. The word `surface(name, tile, project, space, along, seed, base: {…}, vary: {…}, coat: {…},
           coat2: {…}, wear: {…}, dirt: {…}, detail: {…}, sheen, clearcoat, subsurface, …)`. Each layer a
           map with the settings of section 4; sizes arrive in metres (`6 mm` is 0.006). An unknown layer
           or setting fails by name with the nearest right one, as `material` does. `base` is required.
           It gives back a material whose colour, roughness and metal are the base's.
        3. Section 2, decision 10, as problems named plainly: a non-metal base colour outside 0.03 to
           0.9, `metal` between 0 and 1, a pattern with no `size`, two coats with a facing.
        4. Decision 11: `material("oak")` and `material: "oak"` look for a surface made in this scene, then
           `library/surfaces/oak.loop` (brought in as `use` does, its function `oak()` called once and
           its answer kept for the scene), then the built-in list. `materials()` lists all three.
        5. `shadeSurface`: a material with a surface gives the base layer's first colour, roughness and
           metal, flat. (The stack is M3c.)
        6. The help line, a line in docs/LANGUAGE.md under Materials, a "Surfaces" part in modules.json.
Proof:  QUICK; tests: the oak of section 3 parses and every number lands where section 12 says; an unknown
        layer `wera:` fails naming `wear`; a base colour of 0.95 is refused; with a made-up library folder
        holding `surfaces/zz_test.loop`, `material: "zz_test"` finds it and a built-in name still works;
        `./build/loop words` lists `surface`. Picture tests 12 of 12 the same (no library surface exists).
Not:    patterns (M3b), the other layers' effect (M3c), baking (M4), any library entry.
```
```
M3b · The patterns in one place, wrapping                           hand: builder   size: M
Needs:  M3a
Read:   docs/SURFACES.md section 5; engine/render/pattern.cpp (the nine old ones); engine/geometry/noise.h
Touch:  engine/render/surface_patterns.h, surface_patterns.cpp (new), tests/surface_patterns_test.cpp (new)
Do:     1. `void patternAt(const SurfacePattern&, Vec2 u, float tile, uint32_t seed, float& mix, float&
           height, float& piece)` for the fifteen kinds of section 5. `u` in metres.
        2. Every kind wraps at `tile`: lattice points and cells are numbered modulo the count that fits;
           `size` is changed so a whole number of features fits (`fittedSize(pattern, tile)` gives the
           size used, for the cook's warning above 5%).
        3. True shapes: `bricks` in running bond with a 10 mm joint lower than the brick; `planks` with
           ends staggered and a 1 mm gap; `grain` as rings cut by a board (long cathedral arches,
           stretched by `stretch`); `weave` over and under, `twill` stepping the cross-over; `cells`,
           `speckle`, `pores` and `cracks` from wrapped cell noise; `veins`, `strata`, `brushed`, `knit`.
           `checker` and `grid` as today. `piece` the same across one brick, plank, tile, cell.
        4. The old `patternValue` is not touched.
Proof:  QUICK; tests, for every kind at two tiles (1 m and 2.5 m) and two seeds: the value at `u`,
        `u + (tile, 0)` and `u + (0, tile)` agrees to 0.0001 at 200 places; `mix` and `height` stay in 0
        to 1; two seeds differ; the same seed twice agrees exactly. `bricks` of 215 mm in a 2 m tile:
        counting along one course finds 9 whole bricks, and the fitted size is within 5% of asked.
        The test writes `build/patterns.png` (fifteen tiles, `mix` on the left half of each, `height` on
        the right); the report says so, and the lead looks at it before it ships.
Not:    using them (M3c); the shaders (the phone never runs a pattern again).
```
```
M3c · The whole stack in the film renderer                          hand: lead      size: M
Needs:  M2, M3b
Read:   docs/SURFACES.md section 7; engine/render/pattern.cpp; the two `shadeSurface` calls in
        engine/render/pathtracer.cpp
Touch:  engine/render/surface.cpp, engine/render/pattern.h, pattern.cpp, engine/render/pathtracer.cpp,
        tests/surface_test.cpp (new)
Do:     1. `surfaceTile` (steps 1 to 3) and `surfaceAt` (steps 1 to 9, without step 8) as section 7 says,
           line for line. The wander picture: `const Image& wanderPicture()`, 64 square, four smooth
           noises that wrap, the same bytes on every machine.
        2. The path tracer hands `shadeSurface` what it now needs: the place and facing in the thing's
           own space (or the world's, by the recipe), the two marks blended across the triangle from
           `Cook::shared().marks(mesh)` by the hit's triangle and its two weights, and the distance.
        3. Until M6 the tile is laid on from the side the facing points at most, as patterns are today.
        4. Each thing starts at its own place in the tile (a hash of its number).
Proof:  QUICK; tests on `surfaceAt`: marks of 0 give no wear and no dirt whatever the other numbers; edge
        1, amount 1, ragged 0 gives exactly the wear's colour, roughness and metal; dirt lies over wear;
        a coat with cover 0 changes nothing and with cover 1 covers all; over ten tiles the large change
        leaves the average colour within 2% of the base; `surfaceTile` at `u` and `u + tile` agree.
        A picture: `./build/loop render` of a rounded box, a grooved plinth and a ball in red paint (wear:
        bare steel, dirt: near black). Bare steel shows only on the roundings, black only in the groove
        and where the ball meets the plinth; the lead looks at it. Picture tests 12 of 12 the same.
Not:    `box` projection (M6), the detail layer (M7), anything on the phone (M5).
```
```
M4 · The cook bakes a recipe to its pictures                        hand: builder   size: M
Needs:  M3c
Read:   docs/SURFACES.md section 8; engine/geometry/cook.cpp (how a thing is kept by fingerprint);
        engine/core/image.h; engine/core/jobs.h
Touch:  engine/render/surface_bake.h, surface_bake.cpp (new), tests/surface_bake_test.cpp (new)
Do:     1. `struct SurfacePictures { std::vector<Image> colour, shape, detail; }` (each a chain of smaller
           copies, largest first) and `std::shared_ptr<const SurfacePictures> bakeSurface(const
           SurfaceRecipe&, int size)`, kept by the recipe's fingerprint and the size; safe from any
           thread; rows shared over every core.
        2. Colour and shape exactly as section 8's table: four samples a pixel of `surfaceTile`; the
           bumped facing from the height's slope across neighbouring pixels (wrapping at the edges),
           using the true height in metres over the true size of a pixel.
        3. Smaller copies: colour averaged in linear light; the facing averaged then made whole; the
           roughness raised by how much the facings disagreed (Toksvig: from the averaged facing's
           length `L`, the new roughness is `sqrt(r² + (1 - L) / L)`, kept at most 1).
        4. `detail` is left empty here (M7 fills it).
Proof:  QUICK; tests: a recipe with no pattern bakes to one colour (the base's, to 1 in 255), a facing
        of (128, 128) and the base's roughness in every pixel of every copy; `bricks`: the joint's
        pixels are lower (alpha) and their facing leans; the left column continues the right column
        and the top the bottom (their difference is no more than twice the average difference between
        neighbours inside); with `bump` above 0 the third copy's average roughness is higher than the
        first's, with `bump` 0 it is equal; a second ask gives the same pointer; a 256 bake's time is
        printed (wanted: under 200 ms on four cores).
Not:    putting pictures on the GPU (M5); compressed pictures (M62).
```
```
M5 · The phone reads baked pictures for recipe surfaces             hand: lead      size: M (three passes)
Needs:  M2, M4
Read:   docs/SURFACES.md sections 7 and 9; `uploadModelTextures` and the material upload in
        engine/gpu/renderer_scene.cpp; `pattern` in engine/gpu/shaders/common.wgsl.h and its uses in
        scene.wgsl.h, cluster_draw.wgsl.h, ultra.wgsl.h
Touch:  engine/gpu/renderer_scene.cpp, renderer_clusters.cpp, renderer_internal.h, engine/gpu/shaders/
        common.wgsl.h, scene.wgsl.h, cluster_draw.wgsl.h, ultra.wgsl.h, engine/render/frame_cost.cpp,
        tests/pictures/ (one new scene and its reference), tests/frame_cost_test.cpp
Do:     Pass 1. The marks reach the shaders: 4 more bytes a vertex in both ways of drawing (in clusters,
           an extra vertex copies its source's marks). Nothing reads them yet; 12 pictures the same.
        Pass 2. Each surface a scene uses is baked at `render.textureSize` and put in the model
           pictures' stack; one small table holds each surface's numbers for steps 4 to 7; the wander
           picture goes up once.
        Pass 3. The shaders work out steps 4 to 7 for a material with a surface, in the three places a
           material is shaded. The tile laid on from the strongest side until M6.
        Then:  `loop budget` counts the pictures' memory; a picture test `surface_box` (the red-paint
           scene of M3c) joins the 12.
Proof:  QUICK; the web build; `xvfb-run -a node tools/picture_check.js`: the 12 the same after each pass,
        13 of 13 at the end. `./build/loop score` of the red-paint scene: the live picture no further from
        the film's than `showcase_materials` is today (both numbers in the report). On the phone: the user
        plays "Surfaces you want to touch" on the recording page; the graphics chip's time is no more
        than 1 ms above the same room with flat materials. Not accepted before that run.
Not:    `box` and `top` projection (M6); detail (M7); taking the old patterns out of the shaders (when
        the last flat material retires).
```
```
M6 · Projection: `box`, `top`, `uv`, in the thing's space or the world's   hand: builder   size: M
Needs:  M5
Read:   docs/SURFACES.md section 10; `planar` in engine/render/pattern.cpp; where M5 reads the tile in
        engine/gpu/shaders/common.wgsl.h
Touch:  engine/render/surface.h, surface.cpp (one function, `projectSurface`), engine/gpu/shaders/common.wgsl.h
        (the same function), engine/gpu/renderer_scene.cpp (each thing's `along` side and tile start),
        tests/surface_test.cpp, tests/pictures/surface_box.png
Do:     1. `box`: weights are the facing's three parts to the eighth power, the two strongest kept and
           made to add to 1; each side reads the tile along its two other axes, turned so nothing reads
           mirrored on opposite sides; the bumped facing of each side is turned back into the thing's
           space before blending. Film and phone use the same two-sides rule.
        2. `top`: from above. `uv`: the mesh's own unwrap, one unit a metre.
        3. `space`: places measured on the thing as made (`"thing"`) or in the world (`"world"`).
        4. `along`: the pattern's stretched way laid along the mesh's longest side (worked out once a
           mesh, from its bounds) or the named axis.
        5. On the phone, pictures are read with their slopes given from the place itself, so a side
           change never shows as a line of wrong sharpness.
Proof:  QUICK; tests on `projectSurface`: a facing along an axis gives that side a weight of 1; weights
        always add to 1; at 45 degrees between two sides, a half each; turning the facing by one degree
        anywhere never changes a `noise` recipe's colour by more than 2%; a plank recipe on a 2 m by 0.1
        m by 0.1 m box runs along the 2 m; two things with one surface start at different places.
        The picture test `surface_box` is re-made (its reference is part of this order); the lead looks:
        no smeared streaks on the ball's sides. The other 12 the same.
Not:    automatic unwrap (S17); land's cliffs (W5).
```
```
M7 · The detail layer under two metres                              hand: builder   size: S
Needs:  M6
Read:   docs/SURFACES.md section 7 step 8 and section 8
Touch:  engine/render/surface.cpp (step 8), engine/render/surface_bake.cpp (the detail picture),
        engine/gpu/renderer_scene.cpp, engine/gpu/shaders/common.wgsl.h, tests/surface_test.cpp,
        tests/surface_bake_test.cpp
Do:     1. The bake fills `detail` (128 square, wrapping at `detail.tile`) when the recipe has one:
           the bump's facing, the roughness change, the height.
        2. Step 8 in film and on the phone: tilt the facing, add the roughness change, both faded to
           nothing by `fade` and wherever a pixel covers more than half a feature. Laid on by the same
           projection as the tile.
        3. Recipes whose detail has the same numbers share one picture (the fingerprint).
Proof:  QUICK; tests: at `fade` and beyond, `surfaceAt` gives exactly what it gives with no detail; at
        0.3 m the facing differs from the no-detail one; a recipe with no detail bakes none; two recipes
        with the same detail get the same picture. A picture at half a metre of a linen test recipe
        (`weave`, 0.5 mm threads) shows threads in film and on the phone; the lead looks. 13 pictures
        the same (none uses a detail).
Not:    a shared library of detail tiles; fur and pile (M61).
```
```
M8 · The test ball, and a surface's picture                         hand: builder   size: S
Needs:  E5, E6, M3c
Read:   docs/SURFACES.md section 11; the `library picture` command in tools/loop.cpp (E5);
        library/pieces/plinth.loop (an entry to copy the form of)
Touch:  library/models/test_ball.loop (new), tools/loop.cpp (the picture command, for `kind: surface`),
        tests/library_test.cpp
Do:     1. `test_ball(surface, at)`: a ball 0.5 m across on a plinth 0.6 by 0.12 by 0.6 m whose edges are
           rounded at 5 mm and whose top has a groove 1 cm wide and deep round the ball; behind it a
           slab 2 m by 2 m by 4 cm lying flat (to show the tile repeating and the shine at a low angle);
           beside it a 10 cm cube with 2 mm rounded edges (for size). All in the one surface. Its whole
           card: kind model, `size: chair`.
        2. `loop library picture <surface> [--seed n] [--far]`: for an entry of kind surface, places the
           test ball in it under the same sky and sun as other entries. The camera at half a metre
           from the ball's nearest point, 50 mm, the ball two thirds of the frame; `--far` from 3 m and
           1.5 m up, so the slab's far edge is in the frame. Film quality "good", 512 by 512.
Proof:  QUICK; `./build/loop library check`; with the built-in name: `./build/loop library picture oak`
        fails plainly (not a surface entry); with a test surface entry in a made-up library folder
        (tests/library_test.cpp) both pictures are written and are not one flat colour.
Not:    judging; a picture on the phone.
```

**A surface's card.** M9 to M58 are one card each in this form; the lead writes the "Do" from what the real
material is. The asset-smith makes it; the judge marks it; the lead approves it.

```
M<n> · <name>                                                       hand: asset-smith   size: S
Needs:  M5, M6, M7, M8, E7
Read:   docs/SURFACES.md sections 3, 4, 5 and 11; docs/CRAFT.md section 3; docs/rubrics/base.md
Touch:  library/surfaces/<name>.loop only
Do:     the facts of the real material (below), as one recipe under 40 lines with its whole card
Proof:  ./build/loop library check; ./build/loop library picture <name> for seeds 1, 2 and 3, and one
        with --far; the judge's marks on the base rubric for the seed-1 picture: questions 2, 4, 6 and 7
        at 7 or more each, question 10 at 8 or more. At most three rounds of the judge's one change;
        then report honestly with the marks reached and the recipe as it stands.
Not:    a new pattern or setting (if one is missing, stop and say which and why); changing any scene.
Lands:  the lead looks at every old scene that names this material (it now gets the recipe), re-makes
        the picture tests it changes, and says which in the log. The claim says `pictures: yes`.
```
```
M9 · oak                                                            hand: asset-smith   size: S
Do:     European oak, sawn along the trunk, sanded and oiled; for furniture, floors and beams.
        1. Colour: a warm pale brown, brightness about 0.30; the late wood of each ring a third darker.
           Never orange, never yellow.
        2. `grain`: rings 2 to 4 mm wide, stretched 30 to 50 times, arching here and there (a
           plain-sawn board); `tile: 2 m`, `along: "long"`.
        3. Pores: oak's are open and easy to see, short dark dashes 0.3 mm wide and 2 to 5 mm long
           lying along the grain, most in the early wood: the `detail` layer (`pores`, stretched).
        4. Rays: the pale flecks that cross the grain, 5 to 20 mm long, a little shinier: a `coat`
           covering about 4%, `sits: "any"`, roughness 0.15 less than the wood's.
        5. Shine: oiled, roughness about 0.5 to 0.6, lower along the rays; the slow change (about 1 m)
           a tenth in brightness and a little in roughness, as boards differ.
        6. `wear`: paler and smoother where hands and feet polish the edges. `dirt`: dark brown, dull.
        7. `bump` under 0.2 mm: the grain can just be felt.
        8. `seeds: 1 to 4`. `tags: wood, warm, indoor`. `size: chair`.
```
```
M10 · walnut                                                        hand: asset-smith   size: S
Do:     Black walnut heartwood, sanded and waxed to a satin; for fine furniture and panels.
        1. Colour: a deep chocolate brown with a faint purple-grey cast, brightness about 0.10; darker
           streaks, nearly black, a few centimetres wide and long along the grain (a `coat`, covering
           about 15%, soft-edged, stretched 20 times).
        2. `grain`: finer and calmer than oak, rings 1.5 to 3 mm, mostly straight with a slow wave;
           the two ring colours close together (a fifth apart, not a third).
        3. Pores: small and scattered evenly, 0.15 mm wide, 1 to 2 mm long: a fine `detail`.
        4. Shine: satin, roughness about 0.4, more even than oak; the slow change small (0.06).
        5. `wear`: a warmer, lighter brown and a touch shinier (walnut lightens where handled).
           `dirt`: near black, dull, sparing.
        6. `bump` about 0.08 mm: smoother than oak.
        7. `seeds: 1 to 4`. `tags: wood, dark, warm, indoor`. `size: chair`.
```

M11 to M58 follow in the order below, each written out by the lead in the same form when it is next. Your
Hill (milestone 1) needs four of them before the rest: **M20** basalt with varnish, **M27** decomposed
granite, **M28** desert pavement, **M29** dry wash sand; the lead may hand those out first.

```
M11 … M58 · The other forty-eight surfaces, one a card (asset-smith, judged):
     wood: pine, ash, ebony, plywood, weathered board, painted wood, bamboo, cork             (M11 to M18)
     stone: granite, basalt with varnish, limestone, slate, sandstone, marble white, marble dark, travertine  (M19 to M26)
     ground: decomposed granite, desert pavement, dry wash sand, forest floor, wet mud, gravel, turf, snow    (M27 to M34)
     built: red brick, painted brick, cast concrete, polished concrete, plaster, terrazzo, ceramic tile, asphalt  (M35 to M42)
     metal: brushed steel, blackened steel, brass aged, copper green, chrome, cast iron, galvanised, gold leaf (M43 to M50)
     soft: linen, boucle, velvet, wool knit, leather worn, suede, denim, canvas                (M51 to M58)
     (shag pile and felt wait for the fur shell, M61, and take the next free numbers then)
```
```
M59 · Decals (film, then live)                                     hand: lead      size: M
M60 · Night glass: a pane that mirrors the room                    hand: lead      size: M
M61 · A short fur shell for rugs, throws, moss                     hand: lead      size: M
M62 · ASTC pictures made at pack time by a build tool              hand: builder   size: M
```

## 7. Track W · Land (milestone 1 first)

Four things are decided here once, so no card below has to:

1. **The land is one function.** `Land::height(x, z)` is the map's height, the river beds and the fine
   carving together (W1a). Nothing stores the fine land. Every chunk, every plant, every stone and the
   player's feet ask the same function, so they always agree.
2. **The land is drawn as a tree of chunks:** 64 m chunks at 1 m a step near you, 256 m at 4 m to about a
   kilometre (W1b), then 1 km at 16 m and 4 km at 64 m to the map's edge, and beyond it ridgelines drawn
   with the sky (W8). A chunk is shown or its four-by-four children are, never both; skirts hide the joins.
3. **What stands on the land comes in layers** (`engine/geometry/world_layers.h`, W1b). A layer fills one
   chunk with spots (a place, a turn, a size, a kind) by asking the land. Stones, plants, ground cover and
   houses are each a layer in its own file. A layer's answer for a chunk never depends on which chunk was
   made first.
4. **A preset is a library entry** in `library/worlds/`: a function `name(seed)` that calls `world(…)`
   with its numbers, names its ground, its plants and its sky, and lists nature's rules it must keep on
   its card (`rules:`). `loop world <name> --check` holds it to them (W11).

W1 and W3 were each too big for one order and are cut in two. W4, W6, W7 and W10 each add one line to
`engine/script/world_build.cpp`, so those four run one at a time.

```
W1a · The land as one function, carved fine along the flow          hand: lead      size: M
Read:   engine/geometry/world_gen.h; `makeStreamedWorld` in engine/script/scene_api.cpp (how the 4 m
        ground is made today: the river's bed, the noise bumps); docs/CRAFT.md section 6
Touch:  engine/geometry/land.h, land.cpp (new), tests/land_test.cpp (new)
Do:     1. `class Land` made from a `WorldMap` and a seed, safe to ask from any thread:
           `height(x, z)`, `coarse(x, z)`, `normal(x, z, step)`, `slope(x, z)`, `flow(x, z)` (the area
           draining through, from the map), `channel(x, z)` (metres to the nearest water line of any
           level), `wet(x, z)` (0 to 1 from flow and nearness to a channel).
        2. `coarse`: the map's height blended smoothly between its points (a curve through four each
           way, never straight lines: at 1 m a straight blend shows the 32 m grid), less the river's
           bed exactly as `makeStreamedWorld` cuts it today.
        3. Water lines in four levels. Level 0 is the map's own: each cell's middle to the middle of
           the cell its water goes to (`down`). Levels 1 to 3 are grids of 16, 8 and 4 m: each cell
           holds one point, placed by a hash of the cell, the level and the seed; it joins whichever
           is lowest among its eight neighbours' points and the nearest place on a coarser level's
           line within a cell and a half ("lowest" by `coarse` less the carving of the coarser
           levels); a point with nothing lower is a ridge and has no line. Each line is bent once at
           its middle, sideways, by up to a quarter of its length, so gullies wander.
        4. Carving: each level lowers the land by a depth times a V with a rounded floor across its
           line (starting widths 12, 5 and 2 m; depths 2.5, 1.0 and 0.4 m for levels 1 to 3), times
           `clamp(coarse slope / 0.3)` so flats stay flat, deeper where more water passes. Then the
           same amount is given back between the lines, so the land between gullies stands as ridges
           and the average over a map cell does not sink.
        5. A last metre-sized roughness of 0.15 m, only where it is steep.
        6. Asking one place looks at a fixed, small neighbourhood of cells: no answer depends on what
           was asked before. The widths and depths are starting numbers; the tests are fixed.
Proof:  QUICK; tests, on the default world, seed 1, and one other seed:
        a. A 64 by 64 window asked twice, with a far window asked in between, agrees to the last bit.
        b. Water finds a way down at 1 m: in five 128 m windows where the coarse slope is over 0.1,
           from 200 places each, walking to the lowest neighbour, 95% or more leave the window or
           reach a level-0 line without meeting a pit deeper than 0.3 m.
        c. Gullies gather: with water counted downhill across a 1 m window, 80% or more of the cells
           that collect over 500 m² lie within 3 m of a water line.
        d. The shape is kept: over 100 map cells, the average of `height` is within 1 m of the
           average of `coarse`.
        e. Flats stay flat: where the coarse slope is under 0.02, `height` is within 0.2 m of `coarse`.
        f. No cliffs from nothing: where the coarse slope is under 0.3, the slope at 1 m never
           passes 1.2.
        g. 65 by 65 places on a slope in under 20 ms on one core (the time printed).
        The test writes `build/land_detail.png` (a 256 m window at 1 m, lit from the north-west); the
        lead looks for branching gullies that join the map's rivers.
Not:    drawing it or walking on it (W1b); rock that makes cliffs and benches (W2).
```
```
W1b · The streamed world on the function: chunks in a tree, layers   hand: lead      size: M (four passes)
Needs:  W1a
Read:   `StreamedWorld`, `makeStreamedWorld` and `Stage::streamWorld` in engine/script/scene_api.cpp;
        engine/geometry/world_stream.h; `setTerrain` in engine/physics/physics.h and where physics.cpp
        reads it; `height_at` in engine/script/words_land.cpp
Touch:  engine/script/world_build.cpp (new), engine/script/scene_api.cpp, engine/script/words_internal.h,
        engine/geometry/world_stream.h, world_stream.cpp, engine/geometry/world_layers.h (new),
        engine/geometry/terrain.h, terrain.cpp, engine/script/words_land.cpp, tests/world_stream_test.cpp
Do:     Pass 1. Move the three things named above to world_build.cpp. No line changes.
        Pass 2. `WorldStream` becomes a tree with two levels: 256 m chunks at 4 m a step out to 1 km;
           one whose box is within 96 m of the player gives way to its sixteen 64 m children at 1 m.
           Heights from `Land`. Every chunk has a skirt twice its step deep. Still at most
           `makePerStep` chunks made a step, nearest first; a parent stays shown until all its
           children are made.
        Pass 3. The player walks on the same function: the ground given to physics is a patch 128 m
           across at 1 m, filled from `Land` and moved when the player comes within 32 m of its edge
           (`HeightMap` gains a middle that need not be 0, and a function to ask outside its patch).
           `height_at` and the trees ask `Land`. The 4 m ground of the whole world and its noise
           bumps are removed.
        Pass 4. Layers: `struct Spot { Vec3 at; float yaw, size; uint16_t kind, variant; }` and
           `struct WorldLayer { std::string name; float chunk, show, hide; std::function<void(const
           Land&, int cx, int cz, std::vector<Spot>&)> fill; }`. The world keeps a list of layers,
           each with its own stream; `kind` is looked up in a table of meshes and materials the
           script side fills. The trees become the first layer: the same trees in the same places.
Proof:  QUICK after each pass; tests: a chunk and its children are never shown together; walking 500 m
        makes no more than `makePerStep` chunks in any step; the patch's height under the player
        equals `Land::height` to 1 mm before and after it moves; the trees' places before and after
        pass 4 are the same list. `./build/loop budget` of a scene with `grown_island(3)` before and
        after (both in the report); the web build; picture tests 12 of 12 the same (none uses `world`:
        say so if one does). A film picture from the island's start before and after; the lead looks.
        On the phone: the user walks the island on the recording page; no slower than the last run.
Not:    the far levels (W8); ground in blended layers (W5): the seven kinds of ground stay as meshes.
```
```
W2 · Rock in layers: caps, cliffs, benches                          hand: lead      size: M
Needs:  W1a
Read:   engine/geometry/world_gen.cpp (the carving and the slopes at rest); docs/CRAFT.md section 6
        ("hard rock stands, soft rock slopes")
Touch:  engine/geometry/world_gen.h, world_gen.cpp, engine/geometry/land.cpp, engine/script/words_world.cpp,
        docs/LANGUAGE.md, tests/world_gen_test.cpp, tests/land_test.cpp
Do:     1. `WorldSettings::layers`: a list of `{thick (m), hard (0.2 soft … 5 very hard)}` stacked from
           500 m under the sea upward and repeated; `tilt` (degrees) and `tiltToward`; the beds wave
           gently (30 m over a kilometre). `WorldMap::hardAt(x, z, y)`, and `rock` (the layer at the
           surface) and `hard` for every cell.
        2. Rivers cut a rock in proportion to `1 / hard`, read at the land's height at each step.
        3. Slopes rest by their rock: 35 degrees at `hard` 1 and under, rising evenly to 70 degrees
           at `hard` 3 and over.
        4. The fine land (`Land`): gullies cut `1 / hard` as deep; where a hard bed's lower edge
           comes to the surface the land steps (a cliff of up to 70 degrees over the bed's thickness,
           a bench on top), with the step's line following the bed, not the grid.
        5. `world(…, layers: [{thick: 40 m, hard: 3}, {thick: 120 m, hard: 0.6}], tilt: 4 deg)`.
           With no layers, nothing changes.
Proof:  QUICK; tests: with no layers the map's heights are the same to the last bit as before this
        order (keep their fingerprint before starting); with a hard cap over soft rock, on three seeds:
        the average slope of hard cells is 1.5 times the soft cells' or more; of cells steeper than
        45 degrees, 80% or more are hard; no cell is steeper than its rock's angle plus 2 degrees;
        somewhere a river drops 15 m or more between two neighbouring cells as it leaves a hard bed
        (a fall); every old rule still holds (water finds a way down, rivers gather). In `Land`, across
        a bed's edge on a slope, a walk straight downhill meets a step of at least half the bed's
        thickness within 30 m. `./build/loop world 3 -o map.png` with layers; the lead looks for flat
        tops and stepped sides.
Not:    overhangs and caves (W12 and on); falling water (track A).
```
```
W3a · Dry land: no sea, little rain, a flat floor, fans             hand: builder   size: M
Needs:  W2 (the same files)
Read:   engine/geometry/world_gen.cpp, all of it; docs/CRAFT.md section 6 ("deserts show their bones")
Touch:  engine/geometry/world_gen.h, world_gen.cpp, engine/script/words_world.cpp, tools/loop.cpp (the
        map's colours only), docs/LANGUAGE.md, tests/world_gen_test.cpp
Do:     1. `rain` (0 to 2, 1 as now) scales the rain everywhere; forests, grass and snow follow it.
        2. `sea: no`: every plate is land and the map's edge is raised, not sunk, so the middle is a
           basin ringed by ranges; water that reaches a hollow stays there.
        3. `dry: yes`: a hollow fills with what the rivers carried, not with water: a floor level to
           its rim, a new ground `kFlat` (a dry lake bed), no lake.
        4. Fans: where a water line leaves ground steeper than 0.1 onto ground under 0.03, a cone
           is laid down from its mouth: its top 3 to 12 m high by the water it carries, its sides at
           2 to 5 degrees, spreading up to a kilometre; overlapping fans join into an apron.
        5. `start: "hillside"`: the place `world` gives back is on a slope of 10 to 25 degrees, 60
           to 200 m above the floor, facing it. (`"gentle"` is today's rule and stays the default.)
        6. The words: `world(…, rain: 0.1, sea: no, dry: yes, start: "hillside")`.
Proof:  QUICK; tests: with none of the new settings the map is the same to the last bit; with `sea:
        no` there is no sea cell; with `dry: yes` no lake cell, and the largest joined flat (slope
        under 0.01) is a tenth of the map or more; below a range's front, at the mouths of its three
        biggest water lines, the ground within 300 m stands 2 m or more above the floor at a slope
        of 0.02 to 0.1; water still finds a way down (to the floor); the hillside start meets its
        three numbers on five seeds.
Not:    the preset (W3b); what the dry ground looks like (W5).
```
```
W3b · The preset "desert basin"                                     hand: builder   size: S
Needs:  W1b, W3a
Read:   library/worlds/grown_island.loop; docs/CRAFT.md section 1 ("Your hill"); docs/rubrics/hill.md
Touch:  library/worlds/desert_basin.loop (new), tests/world_gen_test.cpp
Do:     1. `desert_basin(seed)` with its whole card (`kind: world`, `place: desert_basin(3)`, `rules:
           drains, gathers, rests, flat_floor 25, closed_basin, cliffs_on_hard`).
        2. Starting numbers: `size: 16 km`, `cells: 385`, `mountains: 0.7`, `sea: no`, `rain: 0.1`,
           `dry: yes`, `warmth: 22`, `layers: [{thick: 60 m, hard: 2.5}, {thick: 200 m, hard:
           0.7}]`, `tilt: 6 deg`, `start: "hillside"`. Sky physical, `haze: 2`; the sun at 50
           degrees, a little to one side of the view over the floor.
        3. Change the numbers, never the generator, until the test below passes on seeds 1 to 5.
Proof:  ./build/loop library check; ./build/loop check library/worlds/desert_basin.loop; a test with
        these numbers on seeds 1 to 5: a quarter or more of the map is floor (slope under 0.01); the
        ranges stand 600 m or more above the floor on two sides at least; from the start, 5 km or
        more of floor can be seen in a straight line; the start is on a slope of 10 to 25 degrees.
        A film picture from the start (`./build/loop render`), in the report.
Not:    its ground, plants, stones, roads or haze: each is added to this entry by its own order.
```
```
W4 · Boulders and scree: where loose rock comes to rest             hand: builder   size: M
Needs:  W1b, W2
Read:   engine/geometry/land.h, world_layers.h; docs/CRAFT.md section 6 ("loose rock rests at about 35
        degrees"); `makeRock` in engine/geometry/plants.h
Touch:  engine/geometry/loose_rock.h, loose_rock.cpp (new), tests/loose_rock_test.cpp (new),
        engine/script/world_build.cpp (one line: the layer joins the list), engine/script/words_world.cpp
        (`rocks:`), docs/LANGUAGE.md
Do:     1. Where stones start: every place steeper than 45 degrees sheds them, more from a higher
           face; and the top of a slope where hard rock comes to the surface breaks out in place.
           Sizes from 0.3 to 3 m, many small and few large (each size half as common as the one two
           thirds its size).
        2. Each stone walks downhill by steps of twice its size along the land's steepest way while
           the ground is steeper than its rest angle: 35 degrees for the smallest, down to 25 for
           the largest (big stones roll further). It stops there, sunk by a fifth to two fifths of
           its size, turned by a hash. 200 m at most.
        3. Where a water line of level 0 or 1 comes out onto ground under 0.05, a fan of stones
           from its mouth, smaller with distance.
        4. Stones under 0.3 m are never placed one by one: `scree(x, z)` (0 to 1) says how thick
           they lie, for the ground's layers (W5).
        5. A chunk's answer counts stones that walk in from 200 m uphill, so it is the same
           whichever chunk is made first; a stone belongs to the chunk it stops in. At most 400
           stones a 64 m chunk, the smallest dropped first.
        6. `world(…, rocks: "basalt_boulder")` names the library's rock family (eight variants by
           seed); with no name, the engine's `rock`. The layer shows to 300 m.
Proof:  QUICK; tests on a small made-up land (a cliff over a ramp that eases to a flat) and on the
        default world: no stone rests on ground steeper than 40 degrees; 70% or more lie within 60 m
        below a cliff or in a fan; the average size in the lower half of an apron is larger than in
        the upper half; a chunk asked twice gives the same spots; four neighbouring chunks hold no
        stone twice; no two stones over 0.5 m overlap by more than a third of the smaller.
Not:    the rock models themselves (library orders); stones that can be pushed (later).
```
```
W5 · The ground in three blended layers                             hand: lead      size: M (three passes)
Needs:  M5, M6, W1b; three ground surfaces in the library (M27 to M29 for the desert)
Read:   docs/SURFACES.md sections 6 to 10; `landMeshes` in engine/script/world_build.cpp; docs/CRAFT.md
        sections 3 and 6
Touch:  engine/geometry/land.h, land.cpp (`ground`), engine/script/world_build.cpp, engine/script/words_world.cpp
        (`ground:`), engine/render/surface.h, surface.cpp (`landAt`), engine/render/pathtracer.cpp, engine/gpu/
        renderer_scene.cpp, engine/gpu/shaders/common.wgsl.h, scene.wgsl.h, cluster_draw.wgsl.h,
        docs/LANGUAGE.md, tests/land_test.cpp, tests/surface_test.cpp
Do:     Pass 1. `world(…, ground: [{surface: "decomposed granite"}, {surface: "desert pavement", slope:
           [12 deg, 30 deg], scree: 0.5}, {surface: "dry wash sand", wet: [0.4, 1]}])`: three surfaces.
           The first is the ground where no rule speaks; each other one says where it belongs by
           `slope`, `wet`, `height`, `scree` (from W4, when it is there) and `ragged` (0 to 1).
           `Land::ground(x, z)` gives the three shares: each rule's fit, softened at its ends, broken
           up by two noises (20 m and 2 m) so borders are ragged with outliers; they add to 1.
        Pass 2. One mesh a chunk, no longer one a kind of ground. The two bytes a vertex that carry
           the marks carry two of the shares. Film: `landAt` works out each surface as section 7
           says and blends them by share and by their own heights (a share is raised where that
           surface's height is high, and only what is within 0.2 of the strongest shows), so stones
           stand up through grit instead of fading into it. `top` projection; `box` above 55 degrees.
        Pass 3. The phone does the same: three surfaces read, the weakest skipped where its share is
           under a tenth; detail from the strongest only.
        Then:  `grown_island` names three surfaces of its own; the seven meshes by kind and their
           seven flat materials leave `world_build.cpp`.
Proof:  QUICK; tests: the shares add to 1 everywhere; flat dry ground gives the first surface 0.9 or
        more; in a water line the wet surface 0.6 or more; a border between two is neither a straight
        line nor a grid step (its length over 100 m is 1.3 times the straight distance or more); the
        same place always gives the same shares. `./build/loop score` from the desert basin's start:
        the live picture no further from the film than the island's was. The judge, hill rubric:
        questions 2 and 8 rise from the picture before. On the phone: the user stands on the hill on
        the recording page; the graphics chip's time is no more than 1.5 ms above the old ground.
Not:    more than three layers; roads (W10 sets a share); wet ground after rain.
```
```
W6 · Elbow-room scatter: each plant keeps its own distance           hand: builder   size: M
Needs:  W1b
Read:   engine/geometry/world_layers.h, land.h; `poissonDisk` in engine/geometry/terrain.h (not used here:
        it cannot be made a chunk at a time); docs/CRAFT.md section 6 ("plants sort themselves")
Touch:  engine/geometry/scatter.h, scatter.cpp (new), tests/scatter_test.cpp (new), engine/script/words_land.cpp
        (`grow`), engine/script/world_build.cpp (one line; copies of a thing), docs/LANGUAGE.md,
        tests/words_test.cpp
Do:     1. `struct PlantKind { name; room; share; slope, wet, height (each a low and a high); facing
           (any, north, south); sizeLow, sizeHigh; near; }`.
        2. `scatterSpots(land, kinds, chunk, seed, out)`: each kind proposes places on a jittered
           grid of its own `room`; each proposal has a rank (a hash). A proposal is kept with a
           chance equal to how well the land there fits the kind (1 inside its ranges, falling to 0
           over a tenth of the range outside: ragged borders, a few outliers), times `share`. Two
           plants must stand at least half the sum of their rooms apart: of two proposals too close,
           the lower rank goes, and a proposal is judged against every proposal near it, kept or
           not, so a chunk's answer needs only its neighbours' proposals and never their answers.
        3. Size: `low + (high - low) × u²`, so young ones are many and old ones few; a turn by hash.
        4. The word, after `world`: `grow(thing, room: 3 m, share: 0.5, slope: [0 deg, 30 deg], wet: [0,
           0.3], height: […], facing: "south", sizes: [0.6, 1.3], seeds: 4, near: 300 m)`. `thing` is a
           function taking a seed, or a library entry's name. It is called once a seed, out of
           sight; what it places is the pattern, and every spot is a copy of one pattern's shapes
           (the same meshes, moved). Each `grow` is a layer; its chunks are half of `near` across.
        5. Plants stand on `Land::height`, sunk 2 cm, and stay off water.
Proof:  QUICK; tests: over four neighbouring chunks no two spots are closer than half the sum of
        their rooms; one kind alone on ground that fits it holds between 0.4 and 0.7 plants for each
        `room` squared of ground; it is no grid (the spread of nearest-neighbour distances is 0.15 to
        0.5 of their average); a kind with `slope: [0, 10 deg]` has 3% or fewer of its plants on
        steeper ground; more plants are below the middle size than above; a chunk asked twice agrees.
        A words test: `grow` with a function that places one box gives boxes that all stand on the land.
Not:    the plants themselves (library orders); small ground cover (W7); anything that grows in time.
```
```
W7 · Ground cover near the player: pebbles, tufts, flowers          hand: builder   size: M
Needs:  W6
Read:   engine/geometry/scatter.h; the `grass` word in engine/script/words_nature.cpp (how many small
        things become one mesh); engine/geometry/cluster.h (separate small pieces get simpler by
        keeping half of them)
Touch:  engine/geometry/cover.h, cover.cpp (new), tests/cover_test.cpp (new), engine/script/words_land.cpp
        (`cover`), engine/script/world_build.cpp (one line), docs/LANGUAGE.md
Do:     1. `cover(thing, per_m2: 4, clump: 0.6, near: 30 m, slope, wet, height, sizes, seeds: 4)`:
           `thing` makes one small thing of 200 triangles or fewer (more is refused, by name).
        2. Places: a jittered grid at the asked number a square metre, kept by how well the land
           fits and by a patch noise raised by `clump` (0 even; 1 in drifts with bare ground
           between, as the yellow flowers lie). Pebbles are sunk by a third; tufts stand upright;
           all turned by hash.
        3. All the copies in a 16 m chunk become one mesh a material, so the clusters thin them
           with distance by themselves. Chunks within half of `near` hold every copy; from there
           to `near`, every fourth, each 1.6 times as big.
        4. Budgets: 60,000 triangles of one cover shown at once, 150,000 of all covers; over that,
           the number a square metre is lowered for the whole scene and the scene is told so, once.
           The number follows the setting `world.foliageDensity`.
        5. Nothing stands inside a stone or a plant's trunk (it asks the other layers' spots).
Proof:  QUICK; tests: on ground that fits, the number a square metre over ten chunks is within 15% of
        asked; with `clump: 0` the counts in 4 m squares vary as chance alone would, with `clump: 1`
        three times as much or more; a chunk's mesh has as many triangles as its copies times the
        thing's; walking 200 m never shows more than the budget; a chunk asked twice gives the same
        mesh (its fingerprint). `./build/loop budget` of a scene with two covers, in the report.
Not:    wind (later); cover that the player flattens.
```
```
W8 · The far view: land to the horizon, ranges beyond                hand: lead      size: M (three passes)
Needs:  W1b, W9; W5 if it has shipped (else the far land keeps its kinds of ground)
Read:   engine/geometry/world_stream.h; engine/script/world_build.cpp; the sky's drawing (`fsSky` in
        engine/gpu/shaders/scene.wgsl.h, engine/render/sky.h); `drawDistance` in engine/gpu/renderer_draw.cpp
Touch:  engine/geometry/world_stream.h, world_stream.cpp, engine/script/world_build.cpp, engine/script/
        words_world.cpp (`beyond:`), engine/geometry/far_ranges.h, far_ranges.cpp (new), engine/gpu/
        renderer_draw.cpp, engine/gpu/shaders/scene.wgsl.h, engine/render/sky.cpp, engine/core/settings.cpp,
        docs/SETTINGS.md, docs/LANGUAGE.md, tests/world_stream_test.cpp
Do:     Pass 1. Two more levels on the tree: 1 km chunks at 16 m and 4 km chunks at 64 m, out to the
           map's edge. A chunk gives way to its children when its step would show as more than
           two pixels at Phone quality. The whole-map mesh that lies 8 m low is removed.
        Pass 2. `world(…, beyond: "ranges" | "sea" | "plain")`. Ranges: three rings of ridgeline
           at 1.5, 2.5 and 4 times the map's half-width, each a strip of about 700 triangles whose
           top comes from ridged noise (as high as 0.6 to 1 of the map's highest peak), each
           further one flatter. On the phone they are drawn with the sky, behind everything, far
           to near, coloured by the haze of W9 at their distance. In film they are real meshes.
        Pass 3. The setting `world.drawDistance` follows the world unless a scene sets it (the
           map's width, at least). Say in the report what the depth's precision is at that
           distance; if far land flickers at Phone quality, report it with numbers and stop.
Proof:  QUICK; tests: a chunk and its children are never shown together at any level; from any of
        20 standing places, the land's triangles shown (before clusters) are under 400,000; 1,000
        rays from the player towards the land each hit a shown chunk (no gap between levels); the
        ranges' strips close their ring. `./build/loop budget` from the desert basin's start within
        the Phone budget. The judge, hill rubric, a film and a live picture from the start:
        question 5 (can you count three ranges) at 6 or more, question 10 no lower than before.
        On the phone: the user turns a full circle on the hill; no slower than before.
Not:    far plants as dots (W12 and on); clouds (A8); the sea's horizon (track A).
```
```
W9 · Haze by distance and height (film and live, one formula)       hand: builder   size: S
Read:   `Fog` in engine/render/scene.h and where the film and the phone apply it; engine/render/sky.h;
        docs/CRAFT.md section 7 ("air")
Touch:  engine/render/sky.h, sky.cpp (`hazeAt`), engine/render/scene.h (`Haze`), engine/render/pathtracer.cpp,
        engine/gpu/shaders/common.wgsl.h, scene.wgsl.h, cluster_draw.wgsl.h, engine/gpu/renderer_scene.cpp,
        engine/script/words_light.cpp (`haze`), docs/LANGUAGE.md, tests/render_test.cpp
Do:     1. `haze(visibility: 60 km, height: 1.5 km)`. Off unless a scene says it. `fog` stays as it
           is, for mist you can stand in.
        2. The air thins with height: `exp(-y / height)`. Along a sight line of length `d` from eye
           height `y0` to a point at `y1`, the air passed is `d × height × (exp(-y0 / height) -
           exp(-y1 / height)) / (y1 - y0)` (or `d × exp(-y0 / height)` when level).
        3. What is lost: `exp(-k × air)` with `k = 3.912 / visibility` for green, 0.75 of that for
           red and 1.5 for blue. So a far dark thing goes blue first, then pale.
        4. What is gained: the sky's colour at the horizon in that compass direction, times what
           was lost from 1, a little brighter towards the sun.
        5. One function, `hazeAt(colour, eye, point, sky)`, in C++ and the same lines in the shader
           file. Film: on the camera's sight line to what it first hits. Phone: where fog is
           applied today, both ways of drawing.
Proof:  QUICK; tests on `hazeAt`: at distance 0 nothing changes; a level sight line as long as the
        visibility leaves 2% of a thing's green; from a higher eye the same distance loses less; a
        black thing at half the visibility is bluer than it is red; a thing at ten times the
        visibility is the horizon's colour to 1%. A scene of five grey walls at 1, 5, 10, 20 and 40
        km: `./build/loop score` puts the live picture within 2 in 255 of the film's. Picture tests 12
        of 12 the same (no old scene asks for haze).
Not:    haze layers behind ridges and shafts (A1); mist with a flat top (A9); clouds.
```
```
W10 · Dirt roads and house plots on the flat (by rules)             hand: builder   size: M
Needs:  W3a, W5, W6
Read:   docs/CRAFT.md sections 1 ("Your hill": the valley) and 6 ("human marks follow the land");
        engine/geometry/land.h, world_layers.h, scatter.h
Touch:  engine/geometry/settle.h, settle.cpp (new), tests/settle_test.cpp (new), engine/geometry/land.h,
        land.cpp (a road's cut, a plot's levelling, `taken`), engine/script/words_land.cpp (`roads`,
        `plots`), engine/script/world_build.cpp (one line), docs/LANGUAGE.md
Do:     1. The flat: joined map cells under 3 degrees.
        2. `roads(every: 800 m, share: 0.6, width: 5 m)`: lines of a square net laid over the flat,
           the whole net turned by up to 5 degrees; each line is there with the chance `share`,
           and runs straight until the land passes 4 degrees or the flat ends. Where it crosses a
           wash it dips with it. One road is carried on to the map's edge. A road is cut 0.15 m
           into the land with shoulders 2 m wide standing 0.1 m proud.
        3. `plots(every: 600 m, share: 0.4, house: "…", shed: "…", trees: "…")`: along the roads, a
           plot 40 by 60 m set 15 m back, levelled to 0.3 m. It holds one house facing the road,
           up to two sheds behind it, up to five trees in a row on its windward side, and a
           watered square of 15 by 20 m. Each is a spot of the named library entry.
        4. `Land::taken(x, z)`: road, plot or neither; stones, plants and cover stay off both. A
           road takes the ground's third surface (W5) across its width. Inside a watered square
           `Land::wet` is 1, so a plant or a cover asked for with `wet: [0.8, 1]` grows there.
        5. The same roads and plots whichever chunk is made first: they come from the map and the
           seed, once, when the world is made.
Proof:  QUICK; tests on a made-up flat with a wash and a hill, and on the desert basin, seed 1: no
        road's ground slopes more than 4 degrees across it or 6 along it; each run lies within 0.5
        m of one straight line; no plot overlaps a road or another plot, and each is within 20 m
        of a road; a plot's ground is level to 0.3 m; no spot of W4, W6 or W7 lies on a road or in
        a plot; on a floor of 20 km² there are 3 km of road or more; twice gives the same.
Not:    the houses, sheds and trees themselves (library orders); paved roads, fences, power lines;
        paths on slopes and terraces (W12 and on).
```
```
W11 · `loop world <preset> --check`: nature's rules for each preset  hand: builder   size: S
Needs:  E6, W3b
Read:   tests/world_gen_test.cpp (the rules as they are tested today); `worldMap` in tools/loop.cpp;
        engine/project/library.h
Touch:  engine/geometry/world_rules.h, world_rules.cpp (new), tests/world_gen_test.cpp, tools/loop.cpp,
        engine/project/library.h, library.cpp (the card line `rules:`), library/worlds/grown_island.loop
        (its `rules:` line), tests/library_test.cpp
Do:     1. Each rule a function of a `WorldMap` (and a `Land` where it needs one) giving its name,
           whether it held, the number found and the number wanted: `drains` (water finds a way
           down), `gathers` (rivers join into bigger ones), `rests` (no slope past its rock's
           angle), `shadow` (the range's far side is drier), `cold_heights`, `flat_floor N` (N% of
           the map is level floor), `closed_basin` (no water leaves), `cliffs_on_hard`, `falls` (a
           river drops off a hard bed somewhere). The tests in world_gen_test.cpp call these
           functions and no longer hold the rules themselves.
        2. A world entry's card may say `rules: drains, rests, flat_floor 25`; `loop library check`
           refuses a rule's name it does not know.
        3. `loop world <name> --check [--seeds 1..5]`: for a library world, calls `name(seed)` for
           each seed, runs its card's rules on the map it made, and prints one line a rule and
           seed (held or not, found, wanted). It ends with 1 if any rule failed. `loop world
           <number>` draws the map as today; `loop world <name> -o map.png` draws the preset's.
Proof:  QUICK; `./build/loop world grown_island --check` holds every rule on five seeds; `./build/loop
        world desert_basin --check` the same; a test with a made-up entry asking `flat_floor 99`
        ends with 1 and names the rule.
Not:    rules for plants, roads and stones (each order's own tests hold those); fixing a preset
        that fails (its numbers are W3b's, or the preset's own order).
```
```
W12 … W40 · alpine river, tower karst, plateau falls, cloud ridge, coast; terraces, paths, ruins; caves
```

**What can start, and in what order (2026-11-10).** With nothing else needed: **M2**, **W1a**, **W9** (three
lanes, no shared files). When E4 ships: **M3a**. Then the two chains:
- surfaces: M3a → M3b → M3c → M4 → M5 → M6 → M7, with M8 beside them once E5 and E6 ship; then M27, M28, M29
  and M20 for the hill, M9 and M10 and the rest as wanted, many at once.
- land: W1a → W1b and W2 (side by side) → W3a → W3b → W11; then W4, W6, W7, W10 one at a time; W5 when M6
  and the three ground surfaces are in; W8 when W9 and W1b are in.

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
