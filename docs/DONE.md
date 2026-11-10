# Done

What is finished, in full: each step, what it did and what was measured. Moved here from `PLAN.md` on
2026-11-10 so the plan holds only what is still to do. Newest work is in `LOG.md`.

## The rebuild · steps 0 to 5 (done 2026-11-06 to 2026-11-08)
0. **The safety net** (done 2026-11-06): a meter on the phone (tap the stats: where each frame's time goes,
   scripts, physics, people, sending, drawing, the graphics chip, memory), picture tests
   (`tools/picture_check.js`, 12 scenes against `tests/pictures/`), a sameness test (the same scene twice, the
   same world; each script now has its own random numbers).
1. **The one clock and the flat world** (done 2026-11-06): the day, the film and the world on one clock;
   drawing blends between steps and gets only what moved (the Glade's sending 1.71 → 0.26 ms).
2. **More than one core** (done 2026-11-06): pictures decoded on every core (the bar's load 4.2 → 1.05 s
   natively), a many-core web player with a one-core fallback, SIMD maths; per-frame physics and people move
   to the cores with steps 7 and 9, where they get heavy.
3. **The cook and the library** (done 2026-11-06): `library/` for every project (5 entries so far), found by
   `use`, placed from the editor, the rule that every goal adds to it; the cook makes each mesh's cluster tree
   once (Nanite's idea, crack-free, tested). Still to come with step 4: leaf cards dropped and enlarged far
   away, collapses along seams (hard-edged models), the cooked trees kept between visits.
4. **Drawing rebuilt (Nanite for WebGPU)** (on by default 2026-11-06): the graphics chip culls and picks the
   detail of every piece itself, one draw, each pixel shaded once (the bar 3.7x faster on the test machine),
   frame blending in multisampling's place, the sun's shadow through clusters, leaf cards simplifying far away
   (2026-11-07). What's left moves into the picture pipeline (`loop budget` counting the new way: its phase 1);
   done along the way: the cooked trees kept between visits (faster loads), hard-edged models simplifying; once
   your phone confirms clusters, the old way shrinks to a fallback for devices that can't draw clusters.
5. **The picture pipeline** (your plan, 2026-11-08): the section below. It takes the place of "smart light"
   (its phases 5 and 6) and adds the foundations under it.

## The picture pipeline (your plan, 2026-11-08) · phases 1-10 done (2026-11-10)
*Your page:* **Loop3D Pipeline Plan: The Strongest Combination**
(https://claude.ai/artifact/VZd81mzPbKVqvXGud7Kmjm); the whole plan, checked against the code: `docs/PIPELINE.md`.
*The idea:* the strongest pipeline is the one closest to the cinema picture inside 16.7 ms on your phone, not the
one with the most techniques. Every choice is judged by two numbers: milliseconds on the phone and distance from
cinema. Each of 50 techniques gets a job (41), waits in reserve (1) or is out with its reason (8).
*Six foundations*, built once, most techniques riding on them: the **frame graph** (each step says what it reads
and writes, and is timed), the **score** against cinema, **one BVH**, **one frame history**, **one light cache**
(probes of bounced light), **one change tracker** (work only where something changed).
*A frame:* update the world, find what's seen, relight the light cache, lamps and shadows, light every pixel
once, fog, blend and upscale, film.

**The phases, in order** (each keeps every scene playing, no slower on the phone, no further from cinema):
1. **Measure** (the tools done 2026-11-08): the recording page times whatever you play and says what went wrong
   when a scene fails (your first visit: the Woods 48 ms on High, the Glade 27 ms on Phone; the whole bar
   didn't load on your phone: its next visit will say why); `loop budget` counts the cluster way (6 of 41
   scenes over the Phone budget: the forests on triangles, the bar on lamps); `loop score` measures each
   picture test against cinema (`perf/score.md`: 23.4 on average, closed rooms lit by the sky, patterns
   missing on Phone, a haze outdoors). Still to come from the phone: the whole bar, and its graphics chip's
   own timers (each recording now lists what the chip offers).
2. **Frame graph** (done 2026-11-08): the frame as named steps that say what they read and write (shadow,
   visibility, light, blend, exposure, bloom, film …), each timed by the graphics chip's own timers where it
   has them (the meter, every recording), the iPhone's 8-buffer limit checked for every pipeline; the same
   picture (12 of 12), the same speed. From your phone still: whether Safari has those timers (your next visit
   to the recording page says).
3. **Change tracker and frame history** (done 2026-11-08): version numbers on what a frame is made from
   (`engine/render/change_tracker.h`); the sun's shadow kept until something it covers changes (your iPhone's
   17 ms in the Glade, now only when something moves or you walk); each pixel's motion, so frame blending
   follows moving things (a moving board about 70% crisper).
4. **Occlusion culling** (done 2026-11-08): pieces hidden behind nearer things are not drawn, tested against
   last frame's depth, and what that skipped tried again against this frame's own, so nothing coming into view
   goes missing. Pieces drawn: the Woods 53,728 to 33,255 (38% fewer), the cabin 40%, the Glade 10%, the bar 2%
   (its room is open: few things are hidden). The meter now says how many pieces are drawn.
5. **Direct light** (started 2026-11-08). *Done first, because your phone's recording said so:* the sun's shadow
   kept far more often: with no sun it is never redrawn (the hall: 0 redraws walking, it was every frame); its
   box moves in steps of an eighth, so walking redraws it every few metres (the courtyard: 10 redraws in 134
   walking frames, was 134; standing still 0, was every frame, because things there move); what moved lately is
   drawn every frame on a copy of the kept shadow of the rest (same picture, checked by `tools/shadow_check.js`).
   The step timers no longer charge a frame's first step with the untimed frame before it.
   *The lamp grid (done 2026-11-08):* the lamps in a tree of groups; each part of the room works out the lamps
   near it one by one and far groups of lamps as one lamp each, so every lamp still lights everything (its shine
   too). The bar: 75 lamps worked out per surface instead of 103 (it truly needs about 50 for 95% of its light:
   its light is spread over many lamps), the same picture (1.8 of 2.0 allowed). The earlier idea of 10 lamps a
   pixel would have dropped the rest of the light: tried, the bar went visibly wrong.
   *Lamp shadows on Phone (done 2026-11-08):* the two main lamps near you cast shadows from shadow maps (six
   views round each bulb), drawn again only when those lamps change (the bar: 5 times walking its length; the
   cabin: once). The cabin on Phone now matches High's traced shadows (0.75 apart; it was 8.5).
   *Sun shadows in steps (done 2026-11-09):* beyond the sharp near map (25 m on Phone, as before) a second,
   coarse map five times as far, kept until its box moves (every ~15 m on Phone) or the scene changes;
   moving things left out of it. Checked: with the near map shrunk to 5 m the far one keeps the Woods'
   canopy shadow (0.89 from the reference; 1.97 without it).
   *Contact shadows (tried 2026-11-09, dropped):* 8 steps across the depth picture towards the sun and the two
   mapped lamps changed 0.03% of the bar's pixels and almost none of the people scene's, and only as faint
   outlines round thin things (a walk catching an object's own edge), while costing every lit pixel. The shadow
   maps already catch what touches at the phone's resolution. Back in reserve, for when something needs it.
   *Phase 5 is done.* Next: phase 6, bounced light.
6. **Bounced light** (done 2026-11-09). *The light cache:* probes through the scene hold the light bounced
   off walls, floors and things (and the sky only where it really gets in), baked by the film renderer on the
   phone's own CPU a few milliseconds a frame, nearest you first (the bar about half a minute, the cabin a few
   seconds; the meter says how many probes are left), sent to the graphics chip as they finish, baked again when
   things or lamps move. Closer to cinema: 23.5 to 20.9 on average (the materials room 35 to 25, the people 35
   to 25, the cabin 18 to 14); the open-air scenes a little further (the Glade +2, fog +1, courtyard +0.6):
   their probes are 10 m and more apart. *Ambient occlusion (done 2026-11-09):* the soft light darkens where
   surfaces meet (corners, under tables, round feet), from 8 points in the depth picture a pixel; it also dims, in
   part, the lamps with no shadow of their own (most of the bar's on Phone). Subtle in these scenes (their light
   is mostly lamps and sun); the cabin 14.2 to 13.5 from cinema; ?view=5 shows it alone
   (docs/ambient_occlusion_2026-11-09.png). *Finer probes outdoors (tried 2026-11-09, not kept):* probes over the
   100 m round you (5 m apart in the Glade and the Woods, not 13-17) took them further from cinema (the Woods 21 to
   25, the Glade 33 to 35-37), as did not counting leaves' undersides as "inside": correct probes under a canopy
   are darker, and the live forest then lacks what cinema has, the sun coming through the leaves. Outdoors the gap
   is the sun through foliage, not the probes. *Sunlit fog (done 2026-11-09):* the fog glows only where the sun
   reaches it (8 points along each view tested against the sun's shadow), so the woods' haze has shade and
   shafts of light between the trees: the fog scene 36.8 to 16.2 from cinema, the Glade 33.5 to 24.9, the average
   20.9 to 18.5 (the Woods 21.3 to 21.7: it looks far past the shadow's 25 m). *Relit near a change (done):* a lamp
   whose light really changes (on a smoothed average: a flickering flame doesn't count), or a thing that moved and
   came to rest, has only the probes near it baked again (putting out the cabin's candle: 320 of 980; checked by
   tools/cache_check.js). *The fog's soft light from the light cache* (a room's haze lit by the room): the fog
   scene 16.2 to 12.5. **Phase 6 is done:** from cinema 23.5 to 18.2 on average over the phase.
7. **Final image** (done 2026-11-09; brought forward on your word: "high end graphics just to blur it"). *Found:* Phone
   drew 1 pixel per screen point on a 3-per-point iPhone (430 x 932 stretched onto 1290 x 2796: a ninth of the
   pixels). *Done:* the picture is now 2 pixels a point (render.ratioPhone 2), and frame blending upscales into it
   from a smaller drawing (16 shifted spots, each full-size pixel taking the sample that landed nearest, the past
   read with a sharp Catmull-Rom curve, a light sharpening after): drawn at the old size it comes out with clean
   edges, 0.91 from a full 2x drawing (the old way 2.13). The automatic resolution chooses how much is drawn.
   *Your recording at 2x (2026-11-09):* the courtyard steady at 60, but the Woods 32 fps (the automatic resolution's
   floor was set for 1 a point; the far sun shadow 54 ms a redraw, its trees' simplest pieces over 25 times the
   area). *Done:* the floor counts from 1 a point (the drawing may go as small as before); the far sun shadow is
   High only; sun shadows pick their pieces' detail by the shadow picture's sharpness; bloom starts from a
   quarter at 2x. *And the bricks:* patterns faded by the drawn pixel's size (early when the drawing is smaller)
   and as a whole by their thinnest line (bricks by their 7 mm mortar: gone 2 m away); now by the shown pixel,
   each brick's or tile's or plank's colour by its own size, and with frame blending detail stays until several
   times smaller than a pixel (blending averages it, as the film renderer's many samples do): bricks, wood grain
   and stone now show (docs/detail_2026-11-09.png).
   *Resolution steered by the step timings (done, phase 7 complete):* with the chip's timers the automatic
   resolution counts only the work that shrinks with it (visibility, lighting; with upscaling, blending, bloom and
   film work on the full picture and don't), leaves out steps that ran only now and then (a shadow redrawn), and
   holds its size when shrinking can't save the time (your High recording: the Glade sank to its floor, 0.25, for
   a far shadow redraw it couldn't shorten). The far sun shadow (High) picks its pieces 5 times coarser, as its
   texels are (it cost 11-23 ms a redraw).
8. **Upgrades** (started 2026-11-09). *Screen reflections (High, on):* glossy floors and counters reflect what's on
   screen, the room itself (the hall's marble floor now shows its glowing door frames), else the light cache's and
   sky's reflection (docs/screen_reflections_2026-11-09.png). *Every lamp casts shadows (High, done, on):* one lamp is traced on its own and the
   rest share one shadow ray a pixel, given each frame to one picked by its light; each pixel keeps its answer
   over the frames where its point was, averaged over up to 32 (ReSTIR's reuse of work over time), so it settles
   in about a second (grain 4.9 after 80 frames, off 3.9; one ray alone 8.6), for the same rays a pixel as before
   (docs/every_lamp_shadows_kept_2026-11-09.png). *From your recording (2026-11-10):* High's two heaviest costs made
   lighter. The bar's lighting (17-21 ms) traced two shadow rays a pixel through its big models: its two main lamps
   now use the shadow maps Phone has (they matched traced shadows in the cabin), and the shared ray for the other
   lamps is traced by half the pixels each frame (a turning checkerboard), the rest keeping their averaged answer:
   a quarter of the rays; the score against cinema the same (the cabin 12.8 to 13.2, the others within 0.1). The far
   sun shadow (the Glade 16-22 ms, the Woods 50 at a redraw) is now drawn a part a frame (about 2.5 ms each,
   timed) into a second layer while the old one is shown, then swapped: the same picture, no stall.
   *Your recording of that (2026-11-10):* the bar on High 45-52 fps (was 22-44), its lighting 7-12 ms; the Glade's far
   shadow 2-3 ms a frame. *Then, better still:* the shared lamp ray traced by a quarter of the pixels a frame (each
   of a 2 x 2 square in turn; the bar's picture the same, grain 8.2 to 8.8); the automatic resolution learns from
   its own timings how much each step really shrinks with the size (the forests' visibility hardly does: it no
   longer blurs the picture for it); and choosing the pieces ("finding") is timed apart from drawing them, so the
   next recording says which of the two the forests pay for.
   *ReSTIR GI on Ultra (built, measured, left off):* each pixel keeps the best bounce path it and its neighbours
   found, reused from frame to frame (`ultra.reuse`). On the cabin, against a 64-frame picture, it came out worse
   (after 4 frames 15.4 from it against 12.2 without; after 16, 5.9 against 5.4): the cabin's light is mostly its
   fire and candle seen straight, and the reuse brings a little bias. Off until a scene shows it helping.
   **Phase 8 is done.** Still the biggest cost on the phone: the forests' "visibility" (their leaves and grass);
   the next recording says how much of it is "finding" the pieces and how much drawing them.
9. **Research** (done 2026-11-10). *Radiance cascades against the light cache* (`loop research cascades`, three rooms,
   the same film-renderer rays for both, against 2,048 rays at each point): as built first they leaked badly (a coarse
   level's rays start out past a wall: 41,000% off in two rooms); with the published fix (only parent rays this probe
   can see the start of) they are as close as a light cache with probes 0.5 m apart (13-17% off against 13-18%)
   for about five times the rays. The light cache stays; its probes closer together would win first.
   *Bidirectional paths in cinema:* the film renderer now also follows light out of the bulbs, through glass and
   off mirrors, to where it lands, and joins that to the camera, so glass bends and focuses a lamp (a glass ball
   over a floor: its bright spot 62.8 against 4.4 with no ball, the light on the floor the same within 3%), where
   before a bulb's light went straight through glass. On whenever a scene has glass and bulbs.
10. **World** (done 2026-11-10). *Typed relationships, the rooms:* the light cache's probes find which neighbours
   they can see (two rays each, so none slips through a seam), so a lamp that changes relights only the probes its
   light can reach (through a door, into the next room; not through a wall). Tested with two rooms: the box round
   the lamp took in the room behind the wall, now none of it. *World generation and streaming (rebuild step 12's
   bare minimum):* `world(seed)` grows an island 8 km across the way nature does (plates lifting ranges, rivers
   carving while it rises, slopes at rest, lakes, rain shadows, sand, grass, forest, rock, snow), checked against
   nature's rules (water always finds a way down, rivers of every size, 35 degree slopes, the far side of the
   mountains dry: 0.70 rain against 0.22); the land and trees are made a chunk at a time round you as you walk and
   hidden behind you (walking 1.5 km: as much shown at the end as at the start). In the Showcase: "A Grown World";
   in the library: `grown_island`; the map: `loop world 3` (docs/world_map_2026-11-10.png,
   docs/world_view_2026-11-10.png). *Next for it:* the mountains are smooth cones of rock (they rest at their
   steepest everywhere): ridges and gullies, then rivers with water in them, then forests that grew.

*Done when:* phases 1 to 7 run smooth on your phone and every scene's score is no worse than today.

## Phase A · Smooth and clean (the foundations) · folded into the rebuild
*You see:* every scene moves smoothly on the phone, with a clean, still picture.

Done:
- **No grain:** live film grain off (`render.filmGrain`), nothing re-rolled every frame (2026-11-05).
- **The work budget:** `loop budget <scene or folder>` counts what a frame costs and what's wasted, without a
  phone; `perf/budget.md` keeps every before and after (2026-11-05).
- **Skip what can't be seen:** each pass draws only what its view sees (`render.cull`); the same picture
  (2026-11-06).
- **Light each pixel once:** the depth pre-pass, automatic from 6 lamps; the bar's lamp work halved (2026-11-06).
- **Simpler distant trees:** two lighter versions of each tree, the shadow pass one simpler; The Woods
  33 M → 3.7 M triangles a frame (2026-11-06).
- Scenes over the Phone budget: **18 of 41 → 9 of 41**.

Its next steps are now the picture pipeline's: the bar's lamps (phase 5), patterns made once and the sun's
shadow kept (phases 3 and 5); frame blending and the forests' simpler versions came with rebuild step 4.

*Done when:* every scene fits the Phone budget, and your benchmark says smooth (steady 60, no hitches).

