# The light lab

The owner, 2026-11-19: the Loft's light "isn't how I like it… I want something fully different, and performance is
already struggling", toward the feel of Unreal's Lumen even by another technique. "Become a scientist: research,
get real test results, and a sheet we update every time."

The sheet: **Light Lab** (claude.ai/artifact/9a9k5xp31ACbTQysgTcLQv), fed from `perf/lab/results.json` and
`perf/lab/methods.json`. Every experiment is one row; nothing is called better without a number.

## The two numbers (every experiment gets both)
1. **Closeness to the film picture**: `loop score` (ΔE, 0 the same, about 2 barely seen, 10+ plainly different)
   between the live picture and the path tracer's picture of the same view. The film picture is the ceiling: no
   live method can look better than the same light done perfectly.
2. **Cost on the phone**: `loop budget` (lamp work per pixel, budget 24) and, once a method is on the player,
   milliseconds timed on the iPhone (the recording page). The lab machine's GPU is software, so its milliseconds
   are not the phone's and are not recorded.

A third question sits beside them: **does the film picture itself look the way the owner wants?** That is the
look (where the lamps are, exposure, tone), judged by the owner's eye, not a number. Kept apart, so a better
method isn't blamed for a lighting design, or the other way round.

## How to run one
`xvfb-run -a node tools/light_lab.js <scene> <name> "<switches>"` (needs `site/`): it settles the live picture as
the picture tests do, reads it back from the GPU, scores it against the film picture (made once per scene, about
8 minutes for the Loft on 4 cores, then reused), and prints one JSON line. `QUALITY=1` High, `QUALITY=2` Ultra.
`./build/loop budget <scene> --set name=value` for the cost.

## The methods (level 1: ours; level 2: the best that exists; level 3: what nobody has done)
See `perf/lab/methods.json` for the list the sheet shows, with each one's verdict as experiments land.

## Round 1 (2026-11-19, the Loft, Phone quality unless named)
Seven experiments; the rows are in `perf/lab/results.json` and the sheet.
- Today: **16.1** from film, **1.8x** too bright. Lamp work 31 (budget 24).
- Bounce off, corner darkening off, lamp shadows off, every lamp shadowed: **16.1** each, pictures within 1 of 255
  of today's. Live has almost no bounced light in the Loft, and none of these parts is where the gap is.
- High (ray shadows, screen reflections): **14.4**, 1.4x. Ultra (rays every frame): **13.1**, 1.5x and grainy.
- So even the film's own method, live, stays 13 away: a large part of the gap is not the bounce method. Next
  round, find it: (1) where live's light on the ceiling and brick comes from when the film's is dark (a lamp
  through its own shade?); (2) whether the light cache bakes anything at night; (3) a still room, baked by the
  film renderer and shown by the phone, to measure how close "film light, looked up" gets.

## Round 2 (2026-11-19): where the gap comes from
Test versions of the room in `perf/lab/scenes/` (each against its own film picture; `SRC=` in `light_lab.js`).
- **Film with no bounce** (`cinema.bounces 1`): the phone is 16.0 from it and 2x too bright, as far as from the
  full film. The phone isn't mainly missing bounce: it adds direct light that the film's geometry blocks.
- **City light only**: 6.0, 4x too bright (it lights the table through the roof). **Lamps inside only**: 15.5.
- **By family**: strips (stair, coves, shelves) **15x too bright**, 10.2; spots 2.2x, 11.0; pendants 1.2x, 11.2.
- **The cause** (`engine/script/words_light.cpp`, `strip`): a strip's stand-in lamps cast no shadows by default,
  and on Phone only `render.lampMaps` (at most 2) lamps have shadow maps. So 44 of 46 lamps light through their
  own channels, shades, shelves, floors and the roof. Lamp grouping, the sky, corner darkening: no change.
- The light cache has 1936 probes baked, yet switching it off changes the picture by 0.05 of 255.
- Ultra held for 200 frames would not settle on the lab's software GPU (stopped after 50 minutes).

**What it means for the method.** The phone can afford shadows for 2 lamps; good rooms have dozens, mostly
hidden in housings. Any live-lamp method pays per lamp for shadows. A bake (light maps) gives every still lamp
its shadows and its bounce for one picture read a pixel, so it attacks the measured cause directly. Round 3:
build the smallest bake (one surface layer of light, made by the film renderer) and score it here.

## Round 3 (2026-11-19): the light bake
Built as `render.bake` (off by default; `engine/render/surface_light.h`, 4 tests). The film renderer's path tracer
works out the light falling on every still surface near the eye once (every lamp with its own shadow, the sun, the
sky, glows, two bounces) and keeps it in cells 10 cm wide, one per way a surface faces, found by a hash of the cell:
no unwrapping, no light-map coordinates. A surface reads the four cells round it in its own plane, blended; the
lamps then add only their shine. The table sits after the lamp lists in their picture (no new binding).
- **The Loft: 10.9 from film, 1.2x bright** (today 16.1 and 1.8x; High 14.4; Ultra 13.1). The ceiling falls dark,
  the table casts its shadow on the rug. 122,525 cells (2.5 MB), baked in about 6 minutes on the lab's computer.
- What's left of the gap: the lamps' shine is still unshadowed (bright streaks on the ceiling slats); the brick
  glows warmer than the film's; 10 cm cells soften small shadows.
- Not yet: baked once and shipped with the scene (today the phone would bake it itself, far too slow), lamps that
  change or move, things that move (they read the bake if near a still surface facing their way), lamp work (the
  lamp loop still runs, for the shine).
Next: ship the bake as a file made by `loop` and time it on the phone; shadow the shine (or drop it for matte
surfaces); 5 cm cells near the eye.
