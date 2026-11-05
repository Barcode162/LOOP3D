# Plan

The order of work, top first. A step is done when it works on the user's phone.

## Next
1. **A clean picture:** no live film grain, nothing that fizzes from frame to frame. *(Built 2026-11-05; the
   phone check is next.)*
2. **See what the phone draws:** pictures of the live renderer taken here (`xvfb-run -a node tools/web_check.js`,
   software graphics, checking only) from the site GitHub's **Check** builds, and a work budget per quality level
   that a scene must fit (things drawn, triangles, lamps per pixel, passes).
3. **Measured on the phone:** the user runs the Benchmark (toolbar ⏱, or `?bench=scene`) on `showcase_glade`,
   `showcase_hall`, an Ardane scene and the bar: the baseline.
4. **Draw less:** skip what's off screen (scene and sun shadows); back faces off for closed shapes; levels of
   detail and clusters (3D's `looped/draw/clusters.*` can come over).
5. **Light less:** each pixel adds only the lamps that reach it (clustered lights: the bar has 103 lamps); wood,
   stone and brick made once into textures; the sun's shadow cached until something moves.
6. **Frame blending (TAA, then temporal upscaling):** fewer pixels drawn, averaged over frames: sharp, steady, no
   noise. Replaces MSAA (settings `render.taa`, `render.upscaler` are waiting for it).
7. **The project file (`.looped`) and the pack:** a project in one folder with one readable project file;
   `loop pack` makes one file the player loads, plays and unloads at will (models and pictures decoded ahead:
   the bar takes 4 s to read natively today).
8. **Templates and the AI loop:** `loop new --template …`, the AI writes from `loop words` and the guide,
   `loop check` names every mistake with its fix, until clean.
9. **One `loop` program for every tool** (the Python and JS helpers folded in).
10. **Bar3D on Loop3D** (`projects/bar3d/TRANSFER.md`): stages 1–4 done (models, textures, the bar as a
    project, its lights alive); next the hidden link (needs the IONOS secrets in this repo's GitHub settings),
    then the games one at a time, then the switch when the user says.
11. **Free hosting:** Cloudflare's free tier for the bar's files, the room, accounts and big files, only if it
    stays free and works.

## Later (the gaps, roughly in order)
- **Content:** the rest of model import (normal maps, glowing pictures, skins and clips); a picture word for a
  loose image; load-once assets by name; the editor saves moved things back to `.loop`.
- **Live look:** light shafts in the live fog (the Glade's fog is tuned for stills: a live-only strength); wind
  sway; cascaded sun shadows; pipelines compiled in the background; contact shadows, ambient occlusion,
  reflections; Ultra with reprojection.
- **Worlds:** streaming big worlds; building scenes off the main thread; WASM threads and SIMD.
- **Characters:** paths around obstacles; skinned meshes (3D has them); clip playback for scripts; faces, hair.
- **Physics:** no tunnelling; collision and trigger events to scripts; convex shapes; ragdolls, vehicles (3D's
  `looped/drive/` has a tyre model).
- **Sound:** the mixer on its own thread; sample playback (music, voice: the bar's jukebox needs it).
- **Engine:** picture tests (before/after diffs); gamepad and rebinding; save games; profiler markers; script
  warnings and a debugger; editor gizmos, snapping, a free camera; multiplayer (3D's Cloudflare room).
- **The user's ideas from 3D:** a shared world clock with loops on things.
- **Every new feature** gets a before/after room in the Showcase.

## Decided
- 2026-11-05: Loop3D is the engine and everything else is a project on it; one page of rules (`CLAUDE.md`);
  quick checks always run; the live-picture checker (software graphics, checking only); **WebGPU only**;
  hosting on Cloudflare only if free; the old rules and tools in `Barcode162/3D` retired. The Showcase and the
  bar's transfer joined `main` (their branches stay as they were); the other session's knowledge base
  (`.claude/knowledge/`) stays only on its branch, `claude/legends-skill-creation-duhg2k`; its `legends` skill
  is kept.

## Known problems (found 2026-11-05, rated 4 / 10)
**Grain:** 8 of the 12 film looks add grain (`engine/render/scene.cpp`) and 15 scenes add more; it was re-rolled
every frame (`fsPost` in `engine/gpu/shaders.h`); Phone draws 1 pixel per screen point (each dot 3 x 3 real
pixels) and automatic resolution lowers it further; pattern bumps from screen-space differences sparkle; Ultra
shows one sample a frame; nothing averages frames. *(Live grain off since 2026-11-05.)*
**Lag:** no culling of what's off screen (every thing drawn every frame, twice: shadow and scene); back faces
drawn (`cullMode None`); every lamp lights every pixel (up to 256; Ardane has 15-22, the bar 103); procedural
patterns computed per pixel per frame (5-octave noise); no depth pre-pass, 4x multisampling on 16-bit colour on
Phone. The processor is not the problem (10 s of a 526-thing scene's script runs in under 0.1 s).
**Never measured on the phone:** `perf/` holds two runs, both on a software GPU.
**Big files in git:** the bar's models (30 MB) and Ardane's stills (24 MB); later: rebuild models from their
recipes, keep packs out of git.
