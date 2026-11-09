# Plan

**The goal:** Loop3D is J Production's one engine, and every game is a project on top of it, the way games are
made in Unreal. Everything is judged on the user's iPhone: smooth first, then beautiful. Phases run in order; a
phase is done when its "done when" is true on the phone.

## Needs you
- **Play on your recording page** (any time, as long as you like): https://claude.ai/artifact/EPvB3EZadvnepQiTiTUXpT.
  Every scene you play is timed and saved for Claude; say "check my runs". Best first: the bar (in the scene
  list as "The whole bar") and The Woods. Your 12 runs of 2026-11-05 came before the cluster drawing (The
  Woods: 6 fps, 159 ms on the graphics chip, 1 ms on the processor); new ones are the picture pipeline's phase 1.
- **The project file's name:** `.looped` is proposed; say if you want another.
- **The bar at its hidden link on joshuavelasco.com** (phase C): the three IONOS secrets added to this repo's
  GitHub settings (only you can).
- **"Switch"**, when you want the live bar to run on Loop3D (phase C).

## The rebuild (your reins, 2026-11-06) · in progress
*Your words:* take the engine apart piece by piece and rebuild it, thinking moves ahead like chess; Nanite,
water, world generation, smart light, a full skeleton, universal animation, films with scenes, physics that fit,
real speed, a library that grows; a ragdoll as real as possible, in layers, that tries to stand up and grabs
hold of things; new, more accurate ways to calculate. The bare minimum of each, smooth and right, then see
where to go. Every step keeps every scene playing and is no slower; what a step replaces goes in that step.

**The moves ahead** (decided once, first, because everything hangs on them):
1. **One clock for everything:** scripts, physics, people, water, film and the world step together; drawing
   blends between steps. The same start always gives the same result, so films scrub by replaying, multiplayer
   stays in sync and picture tests are exact.
2. **The world as flat lists:** one record per thing, with where it was and where it is: safe for many cores,
   the graphics chip gets only what changed, frame blending gets motion for free.
3. **One shape format, clusters** (Nanite's idea): every mesh cut into small pieces, each with simpler
   versions; drawing, shadows, culling, streaming, rays and collisions all use it. No hand-made simpler
   versions ever again.
4. **Recipe, cooked once:** every asset is a text recipe; the engine cooks it (pieces, collision shapes,
   pictures, what it offers) and keeps the result. The library, world generation, the pack and Nanite share it.
5. **Smart light:** worked out once and kept; each frame does only what changed; noisy effects blended over
   frames.
6. **One standard skeleton** with names that match common rigs: any clip plays on anyone, Blender rigs fit.
7. **Water you play is simulated with the world; water you only see is added by the graphics chip;** big waves
   are one formula both use, so what floats matches what you see.
8. **People are always physical bodies; animation is what their muscles aim for:** walking, falling and
   getting up are one body, with numbers from measured human data (masses, lengths, joint ranges, strength,
   reflex delays). One physics solver for everything (bodies, joints, muscles, cloth).
9. **Worlds calculated the way nature makes them** (moving plates, rivers carving, climate, rain, growth), and
   tested against nature's laws (river networks, resting slopes).
10. **The cook finds what each thing offers** (stand on, hold, sit, climb): a falling person reaches for a
    real rail.

**The steps, in order:**
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
6. **The full skeleton and a real body:** about 65 bones (hands, fingers, feet, toes, jaw, eyes), a smooth
   skin that bends, sizes and masses from human measurement tables.
7. **One physics solver for everything:** joints with real limits and muscles, shapes fitted to every model,
   hits in scripts, nothing passing through walls.
8. **Universal animation:** clips as text that play on anyone, layered (wave while walking), the editor's
   Animation hand.
9. **The living body (an active ragdoll in layers):** bones as bodies, muscles with human strength, reflexes
   with a human delay (protect the head, brace, grab a rail), balance the way people keep it (ankle, hip, a
   step), getting up (roll, push, pull up on something); a "falls" room in the Showcase.
10. **Films with scenes:** a timeline of shots, people, lines, music and sounds, scrubbed back and forth.
11. **Water:** waves, splashes, floating, wading; see-through, refraction, foam.
12. **World generation like nature:** plates, rivers carving, climate and rain shadows, lakes, ground
    materials, forests that grew; streamed around you.
13. **Memory that fits:** the iPhone's own compressed pictures, pieces paged in and out.

*Done when:* each step's bare minimum runs smooth on your phone; then we see where to go.

## The picture pipeline (your plan, 2026-11-08) · next: phase 8
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
8. **Upgrades:** ReSTIR DI and screen reflections on High; ReSTIR GI on Ultra.
9. **Research:** radiance cascades against the light cache; bidirectional paths in cinema.
10. **World:** typed relationships and streaming, with rebuild step 12.

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

## Phase B · Projects: the project file, the pack, the AI · next
*You see:* switch between projects from the menu at will; a whole new project made by AI from a template.
1. **The project file:** a project is one folder with one small, readable file (`my_game.looped`: title, the
   engine it needs, its start, its scenes, its settings) beside its scenes, models and sounds.
2. **The pack:** `loop pack` makes one file the player loads, plays and unloads completely (scripts compiled,
   models and pictures prepared ahead: the bar takes 4 s to read today).
3. **Templates and the AI loop:** `loop new --template story`; the AI writes the project from the template, the
   engine's own word list (`loop words`) and the guide; `loop check` names every mistake with its line and fix,
   until clean.
4. **One `loop` program for every tool:** the Python and JavaScript helpers folded in.

*Done when:* Samples and Ardane switch from the menu with memory staying flat, and a project made from a
template by AI plays.

## The Lantern Isle (a game to try the engine, 2026-11-06)
Made to find what the engine can't do yet; waiting for your thoughts on it. Gaps it found are in
`engine/gaps.json` (five fixed, two open: sharp stars, a queue for lines said at once).

## The editor (your reins, 2026-11-06)
One editor for building, logic, film and sound, made for the phone first, answering years of complaints about
Blender, Unity, Unreal, Godot, music and video apps (`docs/EDITOR.md`: each complaint and its rule).
Built: the world view with its own camera, tap to pick, drag to move, handles to lift, turn and size, every
change written into the script; Build, Logic, Film and Sound; undo, redo and versions through reloads; Play
without losing changes; the work light; music with the engine's own instruments (a drum grid, a melody grid).
Next, in order:
1. **Music on the film's timeline:** songs that start with a shot, longer songs, chords.
2. **More hands:** lamps, sun and sky, materials, models, groups, people's walks.
3. **Logic as cards**, the same text underneath.
4. **Saving to the repo from the phone** (needs you).

## The player's menu (your steps, 2026-11-06)
Inspired by 3D's menu button (branch `job/J106`, J106.4) and crosshair (J74.1); one step at a time, you choose.
1. **Clean screen:** one round menu button in the corner; a tap shows the old buttons beside it (done
   2026-11-06, not yet on the public player).
2. **Tap to fan out:** round choices fan out from the button, with **our own icons** (3D's `ui/icons.svg`, 51
   icons drawn for J Production, to recycle).
3. **Hold for everything:** a ring fills, groups open, slide and let go to pick: **the phone's main way**; the
   computer gets its own way (mouse and keys).
4. **The crosshair** (optional).
5. **Its settings** with a live preview; what to recycle from 3D's UI editor (`ui/`: themes like Ember, its
   fonts under the SIL Open Font License, its screens) is looked at then.

## Phase C · Bar3D on Loop3D
*You see:* joshuavelasco.com/bar runs on Loop3D, then the games come back one by one.
Already done (2026-11-05): the bar's 167 models, their pictures, the bar as a project, its lights alive
(`projects/bar3d/TRANSFER.md`; at the hidden link `barcode162.github.io/LOOP3D/#bar`).
1. The bar at a hidden link on joshuavelasco.com (needs you: the IONOS secrets).
2. Sound from files (the jukebox's music).
3. The games one at a time: seats and taps, menus, jukebox, piano, darts, chess, 4K Mania, racing.
4. Together: the multiplayer room, voice and accounts (3D's Cloudflare room), safety first (13+, teens and
   adults apart).
5. The switch, when you say; the old engine retired.

## Phase D · Free hosting
Cloudflare's free tier for the bar's files, the room, accounts and big files; moved only if it stays free and
works.

## Later (the gaps)
(The rebuild now covers skins and clips, cascades, contact shadows, streaming big worlds, threads and SIMD,
skinned people, clips for scripts, convex shapes, hit events, no tunnelling, ragdolls, picture tests and the
meter.)
- **Content:** the rest of model import (normal maps, glowing pictures); a picture word for a loose image.
- **Look:** wind sway (light in the fog, reflections and Ultra's reuse are now picture pipeline phases 5, 8, 9).
- **Characters:** paths around obstacles; faces, hair.
- **Physics:** vehicles (3D has a tyre model).
- **Sound:** the mixer on its own thread.
- **Engine:** gamepad; save games; script warnings and a debugger.
- **Your idea from 3D:** a shared world clock with loops on things.
- **Every new feature** gets a before/after room in the Showcase.

## Decided
- 2026-11-05: Loop3D is the engine and everything else is a project on it; one page of rules (`CLAUDE.md`);
  quick checks always run; the live-picture checker (software graphics, checking only); **WebGPU only**;
  **free** (hosting on Cloudflare only if it stays free); the old rules and tools in `Barcode162/3D` retired; the
  Showcase and the bar's transfer joined `main`; the other session's knowledge base stays on its branch, its
  `legends` skill kept.
- 2026-11-06: the plan in phases (A smooth and clean, B projects, C the bar, D hosting).
- 2026-11-06: **the rebuild** (your reins): the engine rebuilt from its foundations in 14 steps, the ten moves
  ahead above; Phase A folded into it.
- 2026-11-06: **the library**: `library/<kinds>/<name>.loop`, text recipes with a card (credit and licence
  required), found by `use` after the scene's own folder; every goal adds to it.
- 2026-11-08: **the picture pipeline** (your plan, from your page): six shared foundations, 50 techniques each
  given a role, ten phases measured first; it takes the place of rebuild step 5 (`docs/PIPELINE.md`).

## Open problems
- **Measured on the phone only before clusters** (2026-11-05: The Woods 6 fps, the Ardane scenes 12 to 59 fps);
  the cluster build not yet (needs you, above).
- **The bar** is the heaviest scene: 103 lamps per pixel (the picture pipeline, phase 5).
- **Phone draws 1 pixel per screen point;** upscaling (phase 7) is how it gets sharper for the same cost.
- **Big files in git:** the bar's models (30 MB) and Ardane's stills (24 MB); later, models rebuilt from their
  recipes and packs kept out of git.
- The rating of 2026-11-05 (4 / 10) and its findings: `LOG.md` and `perf/budget.md`.
