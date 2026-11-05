# History and lessons

## How it was built (git, 2026-11-03 → 11-05, 32 commits)
1. Core (math, own PNG/zlib, JSON, jobs, tests, CMake) → geometry (shapes, BVH,
   noise, eroded terrain, trees, rocks).
2. Cinema renderer: path tracer, principled BSDF, physical sky, fog, denoiser,
   looks; then instanced two-level BVH.
3. Looped Script 2 (lexer with units, indentation parser, bytecode, fibers, GC).
4. Materials by name, easing, shots; scene words; `loop check/run/render`.
5. Live WebGPU renderer and web player; Ultra (GPU path tracing).
6. Physics, animation, audio; living worlds (solid, person, sound, room).
7. Story words (line, ask, go_to), story guide, Pages workflow (manual only).
8. Ray-traced lamp shadows live (`7346c7f`), which caused the 4 November slowdown.
9. Performance fixes, benchmark + frame recorder + Smoothness Tracker (`perf/`).
10. Projects folder (engine stands alone), the editor, the settings registry wired
    everywhere, then Claude's workspace (`.claude/`).

## The 4 November incident (from docs/TUNING.md §8): the lessons
| What happened | Lesson |
|---|---|
| Lamp-shadow rays went into every pixel of every quality, Phone included (≈10× frame cost) | New per-pixel GPU work is opt-in per quality, and benchmarked on Phone first |
| Auto resolution never fired: it counted skipped frames and summed capped frame times; an overloaded GPU stops the browser asking for frames | Controllers must use real time and the GPU's own done-signals |
| Headless Chromium lost WebGPU, so tests measured the watchdog | Browser checks run under `xvfb-run`, not headless |
| Ultra buffers made on every quality and every resize, over the 128 MB limit → black screen | Allocate per quality, size to adapter limits |
| Every resolution step re-made canvas and all targets; old ones freed only on GC | Draw into part of full-size targets; destroy GPU objects explicitly |
| Up to 6 lamps × 4 rays per pixel, through rain and neon | Shadow casters only; emissive and glass are left out of the tree |
| No recovery from a lost GPU | Recreate the device, resync the scene; step quality down on repeat |
| Ray buffer grew 1.5× each scene change | Size from the current scene, not the last capacity |

## Standing truths
- Measured: one shadow ray per pixel costs about as much as all other lighting.
- Measured: compiling the ray code out when no lamp casts shadows halves Phone's
  pixel cost (shaders reserve registers for their heaviest path).
- Measured: the depth pre-pass gave no gain yet; it is off.
- SwiftShader numbers rank costs; they are not real frame rates. Real devices are
  the truth (the user runs the benchmark and pastes results; `tools/perf_add.py`).
