# The picture pipeline

The user's plan for how Loop3D draws a frame, made the plan on 2026-11-08 from their page
**Loop3D Pipeline Plan: The Strongest Combination** (https://claude.ai/artifact/VZd81mzPbKVqvXGud7Kmjm).
`PLAN.md` holds the order of work; this page holds the whole plan, and what we change in it as we go is written
here.

## The idea
The strongest pipeline is the one that gets closest to the cinema picture inside 16.7 ms on the iPhone, not the
one running the most techniques. Every choice is judged by two numbers: **milliseconds on the phone**, and
**distance from the cinema picture**.

"Use as much as possible" works by giving every technique a job instead of running them all at once. 41 of the
50 get one: live on every tier, an upgrade for stronger devices, a baking tool, a world-building piece, a
reference, a research experiment or a fallback. One is held in reserve, and 8 are out, each with its reason.

The clever part is shared foundations: six pieces of machinery built once, with most techniques riding on them,
so adding a technique costs one new step in the frame, not a new system.

## The six shared foundations
Three already exist; the frame graph, the score and the light cache are new.

| Foundation | What it is | What rides on it | Loop3D today |
| --- | --- | --- | --- |
| Frame graph | Each step of the frame says what it reads and writes; the graph orders the steps, reuses memory, checks the iPhone's 8-storage-buffer limit and times every step | Every step; Phone, High and Ultra become three recipes of the same steps | None: one 2,352-line renderer (`engine/gpu/renderer.cpp`) and one 1,699-line shader file (`engine/gpu/shaders.h`) |
| Reference and score | The cinema path tracer, plus an error score of each live picture against it, per scene | Every decision in this plan; cinema also bakes light | Cinema and Trailer exist (`engine/render/pathtracer.*`, `shots.h`); no score yet |
| One BVH | Boxes inside boxes on the GPU, so rays find triangles fast | Ray-traced shadows, Ultra, probe updates, ReSTIR, baking | Exists: two levels (`engine/geometry/bvh.*`), for lamp shadows on High and for Ultra |
| One frame history | Last frame's picture, depth and motion for every pixel | Frame blending, upscaling, denoising, ReSTIR reuse, occlusion culling, ambient occlusion | Frame blending keeps a history (`render.taa`) |
| One light cache | A sparse grid of probes, each holding the light around it as 9 numbers, the same maths as the sky | Bounced light, rough reflections when screen rays miss, the light inside fog | None (sky light only) |
| One change tracker | Version numbers on everything, so work happens only where something changed | Cluster uploads, sun shadows, probe relighting, sky reflections | Only for sending what moved (rebuild step 1) |

## One frame, step by step
A Phone frame runs eight steps in this order; High and Ultra add to steps 4 and 5, and Ultra's path tracing
replaces the lighting in step 5.

1. **Update the world.** Flat tables on the processor; the change tracker marks what changed, and only that is
   sent to the graphics chip. *Uses: change tracker.*
2. **Find what's seen.** Clusters culled by the view and by last frame's depth, then drawn once into the
   visibility buffer. *Uses: frame history.*
3. **Relight the light cache** (the big new one). A few probes a frame get fresh rays through the BVH, only where
   a lamp or object changed; cinema bakes the starting light. *Uses: BVH, change tracker, light cache.*
4. **Lamps and shadows.** The lamp grid: in the bar each pixel checks about 10 lamps, not 103. Sun shadows in
   steps. Phone: shadow maps for the main lamps. High: ray-traced shadows and ReSTIR DI. *Uses: change tracker;
   the BVH on High.*
5. **Light every pixel once.** PBR materials with grid lamps, sky reflections and bounce light from the cache,
   plus contact shadows and ambient occlusion. High: screen reflections. Ultra: path tracing. *Uses: light
   cache, frame history.*
6. **Fog.** Fog cells lit by the same lamp grid and light cache, instead of building their own. *Uses: lamp
   grid, light cache.*
7. **Blend and upscale.** Drawn at about two-thirds size; past frames rebuild full size and clean up noise. The
   resolution rises and falls with the frame graph's measured timings. *Uses: frame history.*
8. **Film.** Bloom, exposure and AgX tone mapping turn light into the final picture.

Steps 4, 5 and 6 all read the same lamp grid and light cache, which is what keeps a frame with this many
techniques lean.

## The phone's budget
The whole frame gets 16.7 ms on the iPhone for 60 frames a second, and it's a graphics-chip problem: the phone
runs of 2026-11-05 (12 scenes, before cluster drawing; `perf/`) show The Woods at 159 ms of graphics-chip time a
frame and 1 ms of processor time (6 fps), even at half resolution.

Per-step targets stay blank until phase 1 times each step on the phone; guessing them now would steer the work
toward the wrong step. Once measured, going over budget is handled in a fixed order: first the resolution drops
(step 7), then the step that buys the least accuracy per millisecond is turned down.

## Every technique's role
41 of the 50 get a job, 1 waits in reserve and 8 are out. **Have** = Loop3D already runs it; **New live** =
added on every tier; **Upgrade** = High or Ultra only.

| # | Technique | Role | Job, or why it's out |
| --- | --- | --- | --- |
| 1 | Procedural generation | Bake | Terrain and forests made while cooking a project |
| 2 | Scene graph | Out | Flat tables plus typed relationships (4, 6) do its job faster |
| 3 | Streaming | World | Big worlds loaded around the player (rebuild step 12) |
| 4 | Data-oriented design / ECS | Have | The world as flat lists (rebuild step 1) |
| 5 | Incremental updates | Have | Grows into the change tracker |
| 6 | Typed relationships | World | Knows which room a lamp or object is in, so a change relights only that room |
| 7 | Spatial index | Have | The one BVH |
| 8 | Frustum culling | Have | Per cluster, on the graphics chip |
| 9 | Draw per object + LOD | Fallback | Kept only for devices where cluster drawing fails |
| 10 | Occlusion culling | New live | Skipping what's off-screen already took the bar from 1,894 things drawn to the 443 in view; this skips the ones of those 443 hidden behind walls, tested against last frame's depth |
| 11 | Instancing and batching | Have | Folded into cluster drawing |
| 12 | Visibility buffer | Have | Lights each pixel once |
| 13 | Signed distance fields | Reserve | Only if contact shadows and ambient occlusion score poorly against cinema |
| 14 | Clusters / meshlets | Have | Rebuild step 4 |
| 15 | Gaussian splatting | Out | Captured blobs can't be relit by your lamps |
| 16 | Phong / Blinn-Phong | Out | Replaced by PBR (17) |
| 17 | PBR microfacet / GGX | Have | The same material maths as cinema |
| 18 | Layered materials | Have | Clear-coat, sheen, skin, see-through leaves |
| 19 | Image-based light | Have | Re-filtered only when the sun moves |
| 20 | Neural materials | Out | Needs training and research-grade graphics-chip support |
| 21 | Shadow maps | Have | The sun on every tier, plus the main lamps on Phone (phase 5) |
| 22 | Shadow-caster culling | Have | Cluster culling from the sun's view |
| 23 | Cascades and soft filtering | New live | Sun shadows in steps; a step is redrawn only when something in it moves |
| 24 | Tiled / clustered lights | New live | The bar: about 10 lamps a pixel instead of 103; the same grid lights the fog |
| 25 | Contact shadows | New live | Short rays for tiny shadows the maps miss |
| 26 | Ray-traced shadows | Upgrade | High: the two brightest lamps (runs today); Ultra: every lamp |
| 27 | ReSTIR DI | Upgrade | High: shadows from many lamps with one ray a pixel |
| 28 | Baked lightmaps | Bake | Idea kept, storage changed: cinema bakes bounce light into the light cache, no unwrapping |
| 29 | Sky as 9 numbers | Have | Its maths is reused for every probe |
| 30 | Ambient occlusion | New live | Small creases the probes are too coarse for |
| 31 | Light propagation volumes | Out | Probes (35) do the same job better |
| 32 | Screen-space GI and reflections | Upgrade | High: glossy floors and counters; falls back to the light cache |
| 33 | Voxel cone tracing | Out | Too much memory for a phone; probes do the job |
| 34 | Live path tracing + denoising | Upgrade | Ultra |
| 35 | Probe grids / DDGI | New live | The light cache, relit by a few rays a frame through the BVH |
| 36 | Lumen | Out | Too much machinery; its best ideas live in the light cache |
| 37 | ReSTIR GI | Upgrade | Ultra: cleaner bounce light from fewer rays |
| 38 | Radiance cascades | Research | Tested against the light cache with the score |
| 39 | Split radiance cascades | Research | Its sparse storage is borrowed for the light cache; the full method is tested on High |
| 40 | Physical sky | Have | Recomputed only when the sun moves |
| 41 | Exposure and tone mapping | Have | AgX |
| 42 | Bloom and lens effects | Have | Unchanged |
| 43 | Dynamic resolution | Have | Steered by the frame graph's timings |
| 44 | Frame blending (TAA) | Have | Grows into upscaling (47) |
| 45 | Fog | Have | Height fog with the sun's glow today; grows into fog cells lit by the lamp grid (24) and the light cache (35) |
| 46 | Denoisers | Upgrade | Ray-traced shadows on High, and Ultra; shares the frame history |
| 47 | Temporal upscaling | New live | Draw at about two-thirds size, rebuild full size from history (phase 7) |
| 48 | Path tracing | Reference | The cinema renderer: truth and baker |
| 49 | Bidirectional path tracing | Reference | Later: candles behind glass and other hard light |
| 50 | Metropolis light transport | Out | Hard to build; 49 covers your scenes |

## Build order
Measure first, restructure second, add techniques third. Every phase passes the same gate: every scene keeps
playing, nothing gets slower on the phone, and the score against cinema doesn't get worse (or improves, when the
phase is meant to change the look).

1. **Measure.** The phone benchmark on the current cluster build, at least the bar and The Woods. The error
   score against cinema. `loop budget` counting the cluster way. Nothing after this is decided blind.
   *Tools (2026-11-08):* the recording page times whatever is played on the phone (`docs/TUNING.md`, section 5);
   `loop budget` counts the cluster way; `loop score tests/pictures` renders the cinema picture of each picture
   test's view and scores the live one against it (`engine/render/score.h`: ΔE in CIELAB after matching
   brightness, film bars left out; 0 the same, about 2 barely visible, 10 plainly different); the scores are
   kept in `perf/score.md`.
2. **Frame graph.** The renderer split into steps that each say what they read and write, every step timed on
   the phone (if Safari gives no graphics-chip timers, a step is timed by switching it off for a few frames).
   Same picture, same speed: a pure restructure.
   *Done 2026-11-08:* `engine/render/frame_graph.h` (tested natively); the frame is shadow, ultra, visibility,
   light, blend, forward, exposure, bloom, film, its order checked; each step timed by the chip's own timers
   (`timestamp-query`) where it has them, in the meter and in every recorded part (`steps`); every pipeline's
   storage buffers checked against the iPhone's 8 (the visibility and light passes use exactly 8). Pictures 12
   of 12; the same speed measured before and after. Still to learn from the phone: whether Safari has the
   timers (each recording lists the chip's features); without them, the fallback above.
3. **Change tracker and frame history.** Version numbers on graphics-chip work, and each pixel's motion recorded
   from the visibility buffer: the two foundations most later phases plug into.
   *Done 2026-11-08:* `engine/render/change_tracker.h` (tested); its first user, the sun's shadow, drawn again
   only when the placements, shapes, settings or the sun's view (sun, box, eye) change. Each copy's move since
   the last frame drawn (written only for copies that moved); frame blending reads the copy each pixel shows
   from the visibility picture and follows it. Measured: the Glade standing still about a quarter less
   graphics work on the test machine; a board moving at 6 m/s 598 sharp edges with motion, 346 without.
4. **Occlusion culling.** Clusters tested against last frame's depth, then what was hidden tested again. How
   many of the bar's 443 things in view are really seen is counted first, and is the target.
   *Done 2026-11-08:* two passes in the visibility step (Nanite's way). A depth pyramid (each level the farthest
   depth of 2×2 below, half the picture's size at the bottom) is made after the first pass; the cull tests each
   piece's box against last frame's pyramid first, and the second pass draws what that skipped but this frame's
   own pyramid shows. `render.occlude` (on); the meter and every recorded part count the pieces drawn
   (`pieces`: first pass, second). Counted on the test machine: the Woods 53,728 → 33,255 pieces, the cabin
   47 → 28, the Glade 14,042 → 12,661, the bar 2,427 → 2,369 (an open room: the bar's cost is its lamps, phase 5),
   the hall 64 → 64. Pictures 12 of 12. The phone's milliseconds come from its next recording.
5. **Direct light** (the first half of the old "smart light"). The lamp grid (about 10 lamps a pixel in the bar),
   shared with the fog; the unfinished one on `parked/light-grid` (builder, tests and budget counting done) is
   the start. Sun shadows in steps, each redrawn only on change. Shadow maps for the main lamps on Phone. Contact
   shadows.
   *Started 2026-11-08, with the sun's shadow (the phone's recording: 16 to 27 ms a redraw in the Glade and the
   Woods):* no sun, no redraw; the box moves in steps of an eighth of its size and is an eighth bigger, and the
   shadow's detail is judged from the eye rounded to those steps, so walking redraws it every few metres; copies
   that moved in the last two seconds are drawn every frame on a copy of the kept shadow of the rest (a 32-bit
   depth map, copyable; skip bit 2048 draws it all every time, for comparing: `tools/shadow_check.js`). The
   timers: a frame that doesn't follow the last timed one only sets the finish line (the untimed frame's tail
   was charged to the first step, the shadow: 17 ms in the hall, which has no sun).
   *The lamp grid, 2026-11-08* (`engine/render/light_grid.h`, 5 tests): the lamps in a tree (halved along their
   longest side; points with points, panels facing alike together, spots alone); a group is sent as one more lamp
   (its members' light added up at their light-weighted middle, as big as the ball holding them, which softens
   its shine). A grid over the lamps (at most 4,096 cells): each cell lists a cut through the tree, each group
   used only from `render.lampGrouping` (7) of its sizes away from all of the cell, else its halves. Every lamp
   reaches every pixel, alone or in its group. The bar: 75 entries per surface for 103 lamps (`loop budget`),
   pictures 12 of 12 (the bar 1.79). Tried first and dropped: lamps listed only within a reach, the rest as a
   soft per-cell light (the parked design): on average 63% of a bar pixel's lamp light came from the soft part,
   and its missing shine on the glossy floor showed (the bar 10 to 18 of 2.0 allowed).
   *Lamp shadow maps, 2026-11-08* (Phone, where no rays are traced): the first `render.lampMaps` (2) lamps in the
   list (the brightest near the eye; panels go last and never get one) each get six 90-degree views
   (`render.lampMapSize` 256 square), tiles of one depth picture; each view is a cluster list (the still copies
   within 15 m, culled per view), all twelve drawn in one pass with a viewport per tile. Kept by the change tracker
   ("lamp maps": which lamps, the placements, shapes, settings, movers); moving things are left out of them. The
   shader picks the view facing the point and compares (2x2 blended). Checked against High's rays: the cabin 0.75
   apart (8.5 without); the cabin's and the bar's reference pictures renewed.
   *2026-11-09:* each lamp keeps its row (only a newcomer's is drawn, one lamp a frame); a map reaches only where
   its lamp gives a quarter of a lux (2-15 m), fixed when the lamp takes the row (a flickering flame keeps its
   map); a layer per lamp. *Sun shadows in steps:* the shadow map has two layers, the near box as before and a far
   one `kFarShadow` (5) times its size, in steps of its own eighth, drawn with the lamp-view pipeline (its matrix
   in a uniform), moving things left out, kept by the change tracker ("sun far view"). The shader uses the near
   map inside 80% of its box, the far one beyond 96%, blended between, with the same softness in metres. Skip
   bit 4096 turns the far map off; ?shadowdistance= sets the near box (tests).
   *Contact shadows, tried and dropped 2026-11-09:* 8 steps over 0.25 m across the depth picture (read only in the
   light pass) towards the sun and the mapped lamps: 0.03% of the bar's pixels changed, nearly none of the people
   scene's, and only as outlines round thin things; the maps already catch it at Phone's resolution. In reserve.
6. **Bounced light** (the second half). Cinema bakes the light cache; live, a few probes a frame are relit, only
   where something changed. Ambient occlusion fills the small creases.
   *The light cache, 2026-11-09* (`engine/render/light_cache.h`, 6 tests): at most `render.lightCacheProbes`
   (2048) probes over every copy's box, at least 1 m apart. Each is baked by the path tracer's probe mode
   (`PathTracer::probeLight`: lamps and the sun seen straight on left out, the live renderer draws those; with
   more than 8 lamps, 4 picked at each bounce by lumens over distance squared, the bsdf's rays not counting
   lamps), 64 rays, 2 bounces, no ray brighter than 8 times the others' average (fireflies); stored as the light
   on six faces (the first two orders of its spread were tried first: they overstate light from the side, the
   courtyard's shaded walls turned red); a probe whose rays mostly hit backs is inside something and takes its
   neighbours' light; each probe blended 2/5 with its neighbours' 1/10 when sent. The renderer bakes
   `render.lightCacheMs` (3) a frame on the main thread (the phone's page has no threads), nearest the eye first;
   unbaked probes hold the sky's light (as before); the path tracer (37 MB for the bar) lives only while baking,
   stops at once when the scene changes (it points into it) and starts again at most every 2 s. The shader reads
   it for the soft light (6 textures, blended between probes and faces) and dims sky reflections by how much of
   the sky the probes see that way. `loop score --reuse` scores against saved cinema pictures. Scores (cinema at
   32 samples):

```
scene                    score  worst tenth  brightness
showcase_hall             11.2         15.9        1.44
showcase_materials        24.6         36.0        1.39
showcase_light            28.6         76.7        0.52
showcase_sky               5.0         10.1        1.14
showcase_fog              36.7         50.8        1.07
showcase_physics          30.0         87.3        1.26
showcase_people           24.8         47.9        1.28
showcase_glade            33.3         53.6        1.24
courtyard                  9.0         17.3        2.23
cabin                     13.5         25.7        1.38
the_woods                 21.0         30.0        1.96
bar                       11.9         29.6        1.15
average 20.8 over 12 scenes (ΔE: 0 the same picture, about 2 barely visible, 10 plainly different)
```
   *Relit near a change, 2026-11-09:* each lamp's light is smoothed over ~0.7 s; when the average moves more than
   35% from what was baked, the probes within its half-a-lux reach (2-30 m) are marked to bake again
   (`markNear`, `waitingOrder`), as are those round a thing that moved over 25 cm and came to rest (where it was
   and where it is); a fresh path tracer bakes only them, nearest the eye first, at most every 2 s. The cabin's
   candle put out: 320 of 980 probes (`tools/cache_check.js`); its fire, all of them (its light fills the room).
   *The fog's soft light* is the light cache's halfway along each view. Phase 6's scores (cinema at 32 samples):

```
scene                    score  worst tenth  brightness
showcase_hall             11.3         16.7        1.44
showcase_materials        24.4         36.4        1.41
showcase_light            28.6         76.6        0.52
showcase_sky               5.0         10.3        1.14
showcase_fog              12.5         23.9        1.28
showcase_physics          30.0         87.2        1.26
showcase_people           24.8         48.0        1.27
showcase_glade            24.9         45.5        1.12
courtyard                  9.0         17.3        2.23
cabin                     13.5         25.8        1.39
the_woods                 21.7         30.1        1.87
bar                       12.0         30.0        1.16
average 18.2 over 12 scenes (ΔE: 0 the same picture, about 2 barely visible, 10 plainly different)
```
7. **Final image.** Frame blending grows into upscaling; dynamic resolution steered by the frame graph's timings.
   *Upscaling, 2026-11-09:* the picture after frame blending is the full size (outW_ x outH_ at render.ratioPhone
   2 / render.ratioHigh 2 pixels a point); what's drawn is the automatic resolution's share of it. The blend pass
   runs at full size: each pixel takes the drawn sample nearest it (pixel k's sample lies at k + 0.5 + this frame's
   shift; 16 Halton spots), weighted exp(-2.8 d^2) times 0.2, clamps its past (read with a 5-sample Catmull-Rom)
   to the 3x3 colours round that sample in YCoCg, and starts afresh from the drawing blended up. Exposure, bloom
   and film then work at full size (postW_); render.sharpen (0.25) adds to the look's own sharpening. On a 3x
   screen at 430 x 932 points, drawn at 1x: 0.91 from a full 2x drawing (the old stretched 1x: 2.13), edge strength
   1.20 (full 1.22, old 1.08).
   *Resolution steered by the step timings, 2026-11-09* (`engine/render/resolution.h`, 3 tests): the frame's
   steady work split into what goes with the drawn pixels (visibility, light, forward, ultra; and blend, bloom,
   film when not upscaled) and what doesn't (shadow maps; post on the full picture); steps that missed the
   latest timed frame left out. Dropping: to the scale where that split fits 85% of the budget, or holding when
   the part that doesn't scale fills it alone (a frame over twice the budget still steps down). Climbing: when the
   split says the next size up fits. Without timers: the old way (all the time scales). The far sun shadow's
   pieces: the near's detail divided by `kFarShadow`.
8. **Upgrades.** ReSTIR DI and screen-space reflections on High; ReSTIR GI on Ultra.
   *Screen reflections, 2026-11-09 (High, render.ssr):* in the light pass, for a surface rougher than 0.5 nothing;
   else the reflected ray stepped 24 times (0.04 m, then 1.28 times longer each) across the depth picture; a step
   just behind what the eye sees (within 0.1 m + 5% of the way) is the hit, read from last frame's blended picture
   where that point was (the frame's new `prevViewProj`), divided by the exposure; its weight falls near the
   picture's edges and with roughness 0.25-0.5; the light cache's or sky's reflection fills the rest. The light
   pass's group holds both pasts (which is newest flips). Score neutral (thin detail; the score halves pictures).
   *Every lamp's shadow, ReSTIR's first step (High, render.allLampShadows, off):* lamps past the ones traced one by
   one add up, two are picked by their unshadowed light (streaming, a seed a pixel and frame) and traced; the sum
   times their mean visibility. Grain 7.0 (off 3.7, one pick 8.6) on the bar's floor; the bar's score 11.9 off,
   12.0 on. Next: reservoirs kept a pixel and reused over frames (temporal ReSTIR).
9. **Research.** Radiance cascades against the light cache; bidirectional paths in cinema.
10. **World.** Typed relationships and streaming (with rebuild step 12, world generation).

## Where the new research is
Four experiments come almost free once the foundations exist, because each is just a different recipe measured
with the same two numbers.
- **Change-driven light cache.** Relight only the probes a change reaches, instead of a fixed number a frame in
  rotation as DDGI does. Measure milliseconds, the score, and how many frames the picture takes to settle after a
  lamp switches on.
- **Radiance cascades in a browser.** Run them, and the 2026 split version, on our own BVH in small rooms,
  against the light cache. No one was found publishing this on WebGPU.
- **One grid, three jobs.** The lamp grid already lights surfaces; measure what fog costs when it reuses that
  grid and the light cache instead of building its own.
- **Self-tuning tiers.** Rate every step by how much it lowers the error per millisecond it costs; over budget,
  drop the lowest-rated step first, so the tiers are tuned by measurement, not guesswork.

## Checked against the code (2026-11-08)
Three lines differ from the page, because the code says otherwise: the live fog is height fog (fog cells would be
new, technique 45); the bar's 1,894 → 443 was already won by skipping what's off-screen, so occlusion culling's
target is counted first (technique 10, phase 4); and the phone was measured on 12 scenes on 2026-11-05, not one.
The rules the page carries over (from scratch, phone first, eight storage buffers, nothing breaks) are
`CLAUDE.md`'s and the rebuild's.
