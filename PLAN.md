# Plan

**The goal:** Loop3D is J Production's one engine, and every game is a project on top of it, the way games are
made in Unreal. Everything is judged on the user's iPhone: smooth first, then beautiful. Phases run in order; a
phase is done when its "done when" is true on the phone.

## Needs you
- **Benchmark on your phone** (any time, a minute each): open a scene, tap **Benchmark… → This scene**, then
  **Copy results** and paste them in the chat. Best first: the bar (`#bar`), The Woods (`#the_woods`). Every
  number so far came from a software graphics card; yours set the real budget.
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
1. **The one clock and the flat world:** things kept once, not twice; drawing blends between steps.
2. **More than one core:** the work shared by the phone's cores (with a one-core fallback); faster maths.
3. **The cook and the library:** `library/` for every project, each entry a recipe with its credit and
   licence, found by `use`, listed in the editor; meshes cooked into clusters; the rule that every goal adds to
   the library.
4. **Drawing rebuilt (Nanite for WebGPU):** the graphics chip culls and picks the detail of every piece
   itself, one draw a pass, each pixel shaded once; frame blending replaces 4x multisampling.
5. **Smart light:** lamps only where they reach (the bar: 103 a pixel to about 10), kept shadows in steps for
   the sun and for the main lamps on Phone too, bounce light baked by our own path tracer, soft contact
   shadows, upscaling.
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

Its next steps are now rebuild steps: the bar's lamps (step 5), frame blending (step 4), the rest of the
forests (step 4: clusters give every shape its simpler versions), patterns made once and the sun's shadow kept
(step 5).

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
- **Look:** light shafts in the live fog; wind sway; reflections; Ultra with reprojection.
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

## Open problems
- **Never measured on the phone** (needs you, above).
- **The bar** is the heaviest scene: 103 lamps per pixel (phase A, step 1).
- **Shimmer and soft edges** remain until frame blending (phase A, step 2); Phone draws 1 pixel per screen point.
- **The forests' small plants and rocks** have no simpler versions (phase A, step 3).
- **Big files in git:** the bar's models (30 MB) and Ardane's stills (24 MB); later, models rebuilt from their
  recipes and packs kept out of git.
- The rating of 2026-11-05 (4 / 10) and its findings: `LOG.md` and `perf/budget.md`.
