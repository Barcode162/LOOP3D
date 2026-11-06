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

## Phase A · Smooth and clean (the foundations) · in progress
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

Next, in order:
1. **The bar's lamps:** it lights each pixel with 103 lamps, and half of them really light it (many small
   bulbs). Each pixel adds only the lamps that reach it (clustered lights), and the many small bulbs are grouped
   or a few are sampled each frame and blended (with step 2).
2. **Frame blending** (TAA, then temporal upscaling): fewer pixels drawn, averaged over frames: sharp, steady,
   no shimmer; replaces 4x multisampling (`render.taa`, `render.upscaler` wait for it).
3. **The rest of the forests:** grass, flowers, ferns and rocks get simpler versions or leave the shadow pass;
   the farthest trees become flat cut-outs.
4. **Patterns made once:** wood, stone and brick into textures instead of being worked out every pixel, every
   frame; the sun's shadow kept until something moves.

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
- **Content:** the rest of model import (normal maps, glowing pictures, skins and clips); a picture word for a
  loose image; load-once assets by name; the editor saves moved things back to the script.
- **Look:** light shafts in the live fog; wind sway; cascaded sun shadows; contact shadows, ambient occlusion,
  reflections; Ultra with reprojection.
- **Worlds:** streaming big worlds; building scenes off the main thread; threads and SIMD in the browser.
- **Characters:** paths around obstacles; skinned meshes (3D has them); clips for scripts; faces, hair.
- **Physics:** no tunnelling; collision events to scripts; convex shapes; ragdolls, vehicles (3D has a tyre
  model).
- **Sound:** the mixer on its own thread.
- **Engine:** picture tests (before/after, automatic); gamepad; save games; profiler markers; script warnings and
  a debugger; editor gizmos, snapping, a free camera.
- **Your idea from 3D:** a shared world clock with loops on things.
- **Every new feature** gets a before/after room in the Showcase.

## Decided
- 2026-11-05: Loop3D is the engine and everything else is a project on it; one page of rules (`CLAUDE.md`);
  quick checks always run; the live-picture checker (software graphics, checking only); **WebGPU only**;
  **free** (hosting on Cloudflare only if it stays free); the old rules and tools in `Barcode162/3D` retired; the
  Showcase and the bar's transfer joined `main`; the other session's knowledge base stays on its branch, its
  `legends` skill kept.
- 2026-11-06: the plan in phases (A smooth and clean, B projects, C the bar, D hosting).

## Open problems
- **Never measured on the phone** (needs you, above).
- **The bar** is the heaviest scene: 103 lamps per pixel (phase A, step 1).
- **Shimmer and soft edges** remain until frame blending (phase A, step 2); Phone draws 1 pixel per screen point.
- **The forests' small plants and rocks** have no simpler versions (phase A, step 3).
- **Big files in git:** the bar's models (30 MB) and Ardane's stills (24 MB); later, models rebuilt from their
  recipes and packs kept out of git.
- The rating of 2026-11-05 (4 / 10) and its findings: `LOG.md` and `perf/budget.md`.
