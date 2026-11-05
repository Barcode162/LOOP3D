# Plan

The order of work, top first. A step is done when it works on the user's phone.

## Next
1. **A clean picture:** no live film grain, nothing that fizzes from frame to frame. *(Built 2026-11-05; the
   phone check is next.)*
2. **See what the phone draws:** pictures of the live renderer taken here (software graphics in the test
   browser, used only for checking), and a work budget per quality level that a scene must fit.
3. **Measured on the phone:** the user runs the benchmark once (`?bench=scene`): the baseline.
4. **Draw less:** skip what's off screen (scene and sun shadows); back faces off for closed shapes.
5. **Light less:** each pixel adds only the lamps that reach it; wood, stone and brick made once into textures.
6. **Frame blending (a temporal filter):** fewer pixels drawn, averaged over frames: sharp, steady, no noise.
7. **The project file (`.looped`) and the pack:** a project in one folder with one readable project file;
   `loop pack` makes one file the player loads, plays and unloads at will.
8. **Templates and the AI loop:** `loop new --template …`, the AI writes from `loop words` and the guide,
   `loop check` names every mistake with its fix, until clean.
9. **One `loop` program for every tool** (the Python and JS helpers folded in).
10. **Bar3D as a Loop3D project,** piece by piece; the live bar stays on the old repo until the user says switch.
11. **Free hosting:** Cloudflare's free tier for the bar's files, the room, accounts and big files, only if it
    stays free and works.

## Decided
- 2026-11-05: Loop3D is the engine and everything else is a project on it; one page of rules (`CLAUDE.md`);
  quick checks always run; the live-picture checker (software graphics, checking only); WebGPU only; hosting on
  Cloudflare only if free; the old rules and tools in `Barcode162/3D` retired.

## Known problems (found 2026-11-05, rated 4 / 10)
**Grain:** 8 of the 12 film looks add grain (`engine/render/scene.cpp`) and 15 scenes add more; it is re-rolled
every frame (`fsPost` in `engine/gpu/shaders.h`); Phone draws 1 pixel per screen point (each dot 3 x 3 real
pixels) and automatic resolution lowers it further; pattern bumps from screen-space differences sparkle; Ultra
shows one sample a frame; nothing averages frames.
**Lag:** no culling of what's off screen (every thing drawn every frame, twice: shadow and scene); back faces
drawn (`cullMode None`); every lamp lights every pixel (up to 256; Ardane has 15-22); procedural patterns
computed per pixel per frame (5-octave noise); no depth pre-pass, 4x multisampling on 16-bit colour on Phone.
The processor is not the problem (10 s of a 526-thing scene's script runs in under 0.1 s).
**Never measured on the phone:** `perf/` holds two runs, both on a software GPU.
